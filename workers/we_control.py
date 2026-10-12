"""Bounded verification of WEC2 commands; generated Python accessors are not verifiers."""
import os
import struct
import threading

COMMAND_LIMIT = 64 * 1024


class command_verifier:
    def __init__(self, data):
        self.data = data
        if not 16 <= len(data) <= COMMAND_LIMIT or self.scalar(0, "I") != len(data) - 4:
            raise ValueError("Command frame length outside bounds")
        if data[8:12] != b"WEC2":
            raise ValueError("Unknown command identifier")

    def scalar(self, offset, kind):
        width = struct.calcsize("<" + kind)
        if offset < 0 or offset > len(self.data) - width or offset % min(width, 8):
            raise ValueError("Command scalar outside aligned bounds")
        return struct.unpack_from("<" + kind, self.data, offset)[0]

    def table(self, offset, fields):
        distance = self.scalar(offset, "i")
        vtable = offset - distance
        size = self.scalar(vtable, "H")
        object_size = self.scalar(vtable + 2, "H")
        if not distance or not 4 <= size <= 4 + 2 * fields or size % 2 or object_size < 4:
            raise ValueError("Invalid command vtable")
        if vtable < 0 or vtable + size > len(self.data) or offset + object_size > len(self.data):
            raise ValueError("Command table outside bounds")
        return offset, vtable, size, object_size

    def field(self, table, index, kind, default=0):
        offset, vtable, size, object_size = table
        entry = 4 + index * 2
        if entry >= size:
            return default
        relative = self.scalar(vtable + entry, "H")
        if not relative:
            return default
        width = struct.calcsize("<" + kind)
        if relative < 4 or relative + width > object_size:
            raise ValueError("Command field escapes table")
        return self.scalar(offset + relative, kind)

    def reference(self, table, index):
        offset, vtable, size, _ = table
        entry = 4 + index * 2
        if entry >= size:
            raise ValueError("Required command field absent")
        relative = self.scalar(vtable + entry, "H")
        value = self.field(table, index, "I")
        if not relative or not value:
            raise ValueError("Required command reference absent")
        return offset + relative + value

    def string(self, offset, maximum):
        length = self.scalar(offset, "I")
        end = offset + 4 + length
        if not 0 < length <= maximum or end >= len(self.data) or self.data[end] != 0:
            raise ValueError("Invalid command string bounds/terminator")
        raw = self.data[offset + 4:end]
        if b"\0" in raw:
            raise ValueError("NUL in command string")
        return raw.decode("utf-8", errors="strict")

    def decode(self, session):
        root = self.table(4 + self.scalar(4, "I"), 7)
        if self.field(root, 0, "H", 2) != 2 or self.field(root, 1, "H") != 0:
            raise ValueError("Unsupported command version")
        if self.string(self.reference(root, 2), 64) != session:
            raise ValueError("Command session mismatch")
        sequence, operation = self.field(root, 3, "Q"), self.field(root, 4, "Q")
        kind = self.field(root, 5, "B")
        body = self.table(self.reference(root, 6), 1)
        if kind == 1 and sequence == 0 and operation == 1:
            vector = self.reference(body, 0)
            count = self.scalar(vector, "I")
            if not 1 <= count <= 64 or vector + 4 + count * 4 > len(self.data):
                raise ValueError("START argument count outside bounds")
            arguments = []
            for index in range(count):
                at = vector + 4 + index * 4
                relative = self.scalar(at, "I")
                if not relative:
                    raise ValueError("Missing START argument")
                arguments.append(self.string(at + relative, 4096))
            if sum(len(arg.encode("utf-8")) for arg in arguments) > 32768:
                raise ValueError("START argument bytes outside budget")
            return "START", arguments
        if kind == 2 and sequence == 1 and operation == 2 and self.field(body, 0, "Q") == 1:
            return "CANCEL", None
        raise ValueError("Invalid command sequence/operation/target/type")


def read_frame(descriptor):
    def exact(size):
        result = bytearray()
        while len(result) < size:
            block = os.read(descriptor, size - len(result))
            if not block:
                raise ValueError("Command input closed before terminal event")
            result.extend(block)
        return bytes(result)
    prefix = exact(4)
    size = struct.unpack("<I", prefix)[0]
    if not 12 <= size <= COMMAND_LIMIT - 4:
        raise ValueError("Command frame length outside budget")
    return prefix + exact(size)


class command_session:
    def __init__(self, session, writer, cancel_callback):
        self.session, self.writer, self.cancel_callback = session, writer, cancel_callback
        self.cancelled = threading.Event()
        self.selected = None
        self.cancel_seen = False

    def start(self, arguments):
        data = read_frame(0)
        kind, selected = command_verifier(data).decode(self.session)
        if kind != "START":
            raise ValueError("START must be the first worker command")
        self.selected = tuple(selected)
        arguments[1:] = selected
        self.writer.acknowledge("START")
        # Raw os.read avoids a buffered-stdin lock at interpreter shutdown.
        threading.Thread(target=self.listen, name="we-worker-control", daemon=True).start()

    def request_stop(self):
        self.cancelled.set()
        if self.cancel_callback:
            self.cancel_callback()

    def listen(self):
        try:
            for _ in range(64):
                data = read_frame(0)
                if self.writer.terminal:
                    return
                kind, selected = command_verifier(data).decode(self.session)
                if kind == "START":
                    if tuple(selected) != self.selected:
                        raise ValueError("START operation reused with different content")
                first_cancel = kind == "CANCEL" and not self.cancel_seen
                if kind == "CANCEL":
                    self.cancel_seen = True
                self.writer.acknowledge(kind)
                if first_cancel:
                    self.request_stop()
            raise ValueError("Command retry budget exceeded")
        except (ValueError, OSError, UnicodeError, struct.error) as error:
            if not self.writer.terminal:
                try:
                    self.writer.failure("PROTOCOL_INVALID", "control", str(error),
                                        "Committed outputs are retained; restart with a valid command session.")
                finally:
                    self.request_stop()  # A disconnected output must still stop backend work.
