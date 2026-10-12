"""Pinned, generated FlatBuffers events. Stdout is exclusively binary in wire mode."""
import contextlib
import hashlib
import importlib
import os
from pathlib import Path
import sys
import threading

MESSAGE_LIMIT = 1024 * 1024
WRITER = None
CONTROL = None


class event_writer:
    def __init__(self, bindings, stream, session):
        sys.path.insert(0, str(Path(bindings).resolve(strict=True)))
        self.runtime = importlib.import_module("flatbuffers")
        if self.runtime.__version__ != "25.2.10":
            raise RuntimeError("FlatBuffers 25.2.10 runtime is required")
        self.modules = {name: importlib.import_module("world_explorer.wire." + name)
                        for name in ("envelope", "handshake", "progress", "artifact_reference",
                                     "completed", "failure", "payload", "command_acknowledged", "command_kind")}
        self.session = session
        self.lock = threading.RLock()
        self.stream = stream
        self.sequence = 0
        self.terminal = False

    def table(self, builder, name, values):
        module = self.modules[name]
        module.Start(builder)
        for field, value in values.items():
            getattr(module, "Add" + field)(builder, value)
        return module.End(builder)

    def write(self, builder, name, value):
        with self.lock:
            if self.terminal:
                return
            if self.sequence >= 10000:
                raise RuntimeError("Worker event lifecycle exceeded")
            session = builder.CreateString(self.session)
            body = self.table(builder, "envelope", {"ApiMajor": 2, "Sequence": self.sequence,
                              "Session": session, "Operation": 0 if name == "handshake" else 1,
                              "BodyType": getattr(self.modules["payload"].payload, name), "Body": value})
            builder.FinishSizePrefixed(body, file_identifier=b"WEW2")
            data = builder.Output()
            if len(data) - 4 > MESSAGE_LIMIT:
                raise RuntimeError("Worker event exceeds message limit")
            self.stream.write(data)
            self.stream.flush()
            self.sequence += 1
            self.terminal = name in ("completed", "failure")

    def acknowledge(self, kind):
        builder = self.runtime.Builder(128)
        value = self.table(builder, "command_acknowledged", {
            "CommandSequence": 0 if kind == "START" else 1,
            "CommandOperation": 1 if kind == "START" else 2,
            "Kind": getattr(self.modules["command_kind"].command_kind, kind)})
        self.write(builder, "command_acknowledged", value)

    def handshake(self, *, backend, backend_version, sensors, devices, checkpoint_semantics,
                  checkpoint_rights, limits):
        builder = self.runtime.Builder(512)
        strings = {name: builder.CreateString(value) for name, value in {
            "Backend": backend, "BackendVersion": backend_version,
            "CheckpointSemantics": checkpoint_semantics, "CheckpointRights": checkpoint_rights}.items()}
        for name, values in (("Sensors", sensors), ("DeviceOptions", devices)):
            offsets = [builder.CreateString(value) for value in values]
            getattr(self.modules["handshake"], "Start" + name + "Vector")(builder, len(offsets))
            for offset in reversed(offsets):
                builder.PrependUOffsetTRelative(offset)
            strings[name] = builder.EndVector()
        value = self.table(builder, "handshake", strings | {
            "Batch": True, "StreamingTracking": False, "MaxMessageBytes": MESSAGE_LIMIT,
            "MaxFrames": limits["max_frames"], "MaxSourceBytes": limits["max_source_bytes"],
            "MaxOutputBytes": limits["max_output_bytes"]})
        self.write(builder, "handshake", value)

    def progress(self, stage, message, elapsed=0):
        builder = self.runtime.Builder(256)
        values = {"Stage": builder.CreateString(stage), "Message": builder.CreateString(message),
                  "ElapsedSeconds": elapsed}
        self.write(builder, "progress", self.table(builder, "progress", values))

    def completed(self, stage, artifact=None):
        builder = self.runtime.Builder(256)
        reference = 0
        if artifact is not None:
            path = Path(artifact).resolve(strict=True)
            size = path.stat().st_size
            if not 0 < size <= MESSAGE_LIMIT:
                raise RuntimeError("Completion manifest exceeds wire budget")
            data = path.read_bytes()
            if len(data) != size:
                raise RuntimeError("Completion manifest changed during read")
            reference = self.table(builder, "artifact_reference", {
                "Path": builder.CreateString(str(path)),
                "Sha256": builder.CreateString(hashlib.sha256(data).hexdigest()), "Bytes": size})
        value = self.table(builder, "completed", {"Stage": builder.CreateString(stage), "Artifact": reference})
        self.write(builder, "completed", value)

    def failure(self, code, stage, message, recovery):
        builder = self.runtime.Builder(256)
        values = {key: builder.CreateString(value) for key, value in {
            "Code": code, "Stage": stage, "Message": message, "Recovery": recovery}.items()}
        self.write(builder, "failure", self.table(builder, "failure", values))


def configure(arguments, *, cancel_callback=None, **handshake):
    global WRITER, CONTROL
    if "--wire-bindings" not in arguments:
        return
    if len(arguments) != 5 or arguments[1] != "--wire-bindings" or arguments[3] != "--wire-session":
        raise ValueError("Wire mode requires only explicit bindings/session bootstrap arguments")
    bindings, session = arguments[2], arguments[4]
    if len(session) != 64 or any(character not in "0123456789abcdef" for character in session):
        raise ValueError("Invalid worker session")
    if os.name == "nt":
        import msvcrt
        msvcrt.setmode(0, os.O_BINARY)
        msvcrt.setmode(1, os.O_BINARY)
    WRITER = event_writer(bindings, sys.stdout.buffer, session)
    WRITER.handshake(**handshake)
    from we_control import command_session
    CONTROL = command_session(session, WRITER, cancel_callback)
    try:
        CONTROL.start(arguments)
    except (ValueError, OSError, UnicodeError) as error:
        WRITER.failure("PROTOCOL_INVALID", "control", str(error), "Restart with a valid START command.")
        raise SystemExit(2)


def cancel_requested():
    return bool(CONTROL and CONTROL.cancelled.is_set())


def diagnostic_context():
    return contextlib.redirect_stdout(sys.stderr) if WRITER else contextlib.nullcontext()


def progress(stage, message, elapsed=0):
    if WRITER:
        WRITER.progress(stage, message, elapsed)
    else:
        print(message, flush=True)


def completed(stage, artifact=None):
    if WRITER:
        WRITER.completed(stage, artifact)


def failure(code, stage, message, recovery="Committed outputs are retained. Correct the cause and retry."):
    if WRITER:
        WRITER.failure(code, stage, message, recovery)
    else:
        print(message, file=sys.stderr, flush=True)
