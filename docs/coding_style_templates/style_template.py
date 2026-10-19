"""Reference style sheet for Python 3.15 code (TEMPLATE: edit to match house style).

HOW TO USE THIS FILE
    * Every convention is demonstrated in code. Lines tagged ``# [DECISION]``
      mark a point where reasonable teams differ; change the example, and the
      tag text, to record YOUR choice. Delete the tag once settled.
    * Lines tagged ``# [RULE]`` state a rule that should be followed verbatim.
    * Sections are numbered so reviewers can cite them ("violates §7.2").
    * This file must stay importable and pass the project's linters/type
      checker. Treat it as executable documentation.

TABLE OF CONTENTS
    1. File layout, encoding, formatting
    2. Naming
    3. Imports (incl. lazy imports)
    4. Constants, sentinels, type aliases
    5. Enums
    6. Data containers (dataclass, NamedTuple, TypedDict, frozendict)
    7. Classes (ABC, Protocol, slots, properties, dunders, generics)
    8. Functions (signatures, overloads, decorators, generators)
    9. Typing conventions
   10. Control flow (match, comprehensions, walrus)
   11. Strings (f-strings, t-strings, encoding)
   12. Errors, exceptions, exception groups
   13. Resources, context managers, pathlib, I/O
   14. Concurrency (asyncio, threads, processes)
   15. Logging, warnings, deprecation
   16. Comments, docstrings, TODOs
   17. Testing conventions
   18. Entry points

SUGGESTED TOOLING (set to what you actually use)               # [DECISION]
    formatter: ruff format | linter: ruff | types: mypy --strict (or pyright)
    line length: 88 | quotes: double | docstrings: Google style
"""

# ============================================================================
# 1. FILE LAYOUT, ENCODING, FORMATTING
# ============================================================================
# [RULE] Source files are UTF-8, LF line endings, one trailing newline.
# [RULE] Order inside a module: docstring -> __future__ (if any) -> imports ->
#        __all__ -> constants/aliases -> private helpers -> public API -> main.
# [DECISION] Max line length 88; wrap with parentheses, never backslashes.
# [DECISION] 4-space indent; magic trailing comma on every multi-line literal.
# [DECISION] Two blank lines between top-level defs, one between methods.
# [DECISION] Double quotes for strings; single quotes only to avoid escapes.
# [RULE] No ``from __future__ import annotations``: since 3.14 annotations are
#        lazily evaluated by default, so forward references need no quotes.

# ============================================================================
# 3. IMPORTS
# ============================================================================
# [RULE] Group order, blank line between groups, alphabetical within groups:
#        1) standard library  2) third party  3) first party / local.
# [DECISION] Absolute imports only; relative imports forbidden (or: allowed
#            within the same package for siblings).
# [DECISION] ``import x`` for modules, ``from x import Y`` for names;
#            never ``from x import *``.
# [DECISION] Alias only by established convention (np, pd) or to resolve clash.

import asyncio
import contextlib
import dataclasses
import enum
import functools
import logging
import re
import sys
import warnings
from abc import ABC, abstractmethod
from collections import defaultdict
from collections.abc import (
    AsyncIterator,
    Callable,
    Iterable,
    Iterator,
    Mapping,
    Sequence,
)
from dataclasses import dataclass, field
from pathlib import Path
from typing import (
    TYPE_CHECKING,
    Any,
    ClassVar,
    Final,
    Literal,
    NamedTuple,
    Protocol,
    Self,
    TypedDict,
    assert_never,
    final,
    overload,
    override,
    runtime_checkable,
)

# [DECISION] Lazy imports (new in 3.15, PEP 810): use ``lazy import`` for heavy
# modules only needed on rare code paths, to cut startup time. Do NOT make
# lazy any module whose import has required side effects (plugin registries,
# signal handlers, monkey-patches) or that you need at import-time validation.
lazy import json
lazy import subprocess
lazy from decimal import Decimal

# [DECISION] Type-only imports go under TYPE_CHECKING when they would create a
# cycle or are expensive. Annotations need no quoting (see §1).
if TYPE_CHECKING:
    from collections.abc import Awaitable

# [RULE] Third-party imports would be here, e.g.:
# import httpx
# import pydantic

# [RULE] First-party imports would be here, e.g.:
# from myproject.core import settings

__all__ = [  # [RULE] Every public module defines __all__; sorted alphabetically.
    "DEFAULT_TIMEOUT_S",
    "Color",
    "Config",
    "Repository",
    "RetryPolicy",
    "main",
    "parse_duration",
]

# ============================================================================
# 4. CONSTANTS, SENTINELS, TYPE ALIASES
# ============================================================================
# [RULE] Constants: UPPER_SNAKE_CASE, module level, annotated with Final.
# [DECISION] Put the unit in the name when it is not obvious (_S, _MS, _BYTES).
DEFAULT_TIMEOUT_S: Final = 30.0
MAX_RETRIES: Final[int] = 3
_DURATION_RE: Final = re.compile(r"(?P<value>\d+)(?P<unit>ms|s|m|h)")

# [RULE] Mutable constants are forbidden; use tuples, frozenset, frozendict.
# frozendict is a builtin since 3.15.
SUPPORTED_UNITS: Final = frozendict({"ms": 0.001, "s": 1.0, "m": 60.0, "h": 3600.0})
ALLOWED_SCHEMES: Final = frozenset({"http", "https"})

# [DECISION] Use the builtin ``sentinel`` (3.15) for "argument not provided"
# when None is a legitimate value. Never use ``object()`` or magic strings.
MISSING: Final = sentinel("MISSING")

# [RULE] Type aliases use the ``type`` statement (PEP 695), PascalCase.
type JSON = dict[str, JSON] | list[JSON] | str | int | float | bool | None
type Handler[T] = Callable[[T], None]
type Headers = Mapping[str, str]

# ============================================================================
# 5. ENUMS
# ============================================================================
# [RULE] Enum names PascalCase (singular noun), members UPPER_SNAKE_CASE.
# [DECISION] StrEnum for values that cross serialization boundaries;
#            IntEnum/IntFlag only for wire/C-interop numbers; else Enum.


class Color(enum.StrEnum):
    """Named colors used in output."""

    RED = enum.auto()
    GREEN = enum.auto()


class Permission(enum.Flag):
    """Bit-flag permissions."""

    READ = enum.auto()
    WRITE = enum.auto()
    ADMIN = READ | WRITE


# ============================================================================
# 6. DATA CONTAINERS
# ============================================================================
# [DECISION] Choose by need:
#   - dataclass(frozen=True, slots=True, kw_only=True)  -> default for records
#   - NamedTuple   -> small, positional, tuple-compatible return values
#   - TypedDict    -> typed shapes of external dicts (JSON); no behavior
#   - frozendict   -> immutable mapping with arbitrary keys
#   - pydantic/attrs -> only when runtime validation is required


@dataclass(frozen=True, slots=True, kw_only=True)
class RetryPolicy:
    """Immutable retry configuration.

    Attributes:
        max_attempts: Total tries including the first. Must be >= 1.
        backoff_s: Initial delay in seconds; doubles each attempt.
        retry_on: Exception types that trigger a retry.
    """

    max_attempts: int = MAX_RETRIES
    backoff_s: float = 0.5
    retry_on: tuple[type[Exception], ...] = (TimeoutError, ConnectionError)

    def __post_init__(self) -> None:
        if self.max_attempts < 1:
            raise ValueError(f"max_attempts must be >= 1, got {self.max_attempts}")


@dataclass(slots=True, kw_only=True)
class Config:
    """Mutable configuration. [RULE] Mutable defaults use ``field(default_factory=...)``."""

    name: str
    tags: list[str] = field(default_factory=list)
    retry: RetryPolicy = field(default_factory=RetryPolicy)
    timeout_s: float = DEFAULT_TIMEOUT_S


class Point(NamedTuple):
    """2-D point. [RULE] NamedTuple fields documented in the class docstring."""

    x: float
    y: float


class UserRecord(TypedDict):
    """Shape of a user object returned by the API."""

    id: int
    name: str


class UserRecordPatch(TypedDict, total=False):
    """Partial update; all keys optional."""

    name: str


# ============================================================================
# 7. CLASSES
# ============================================================================
# [RULE] Class names PascalCase; exceptions end in Error; ABCs/Protocols are
#        named for the capability (``Closeable``) or role (``Repository``).
# [DECISION] Prefer composition and Protocols over inheritance; inheritance
#            depth <= 2 outside of exceptions and ABCs.
# [DECISION] Member order: ClassVars, __slots__/fields, dunder constructors,
#            other dunders, classmethods/staticmethods, properties, public
#            methods, private methods.


@runtime_checkable
class Closeable(Protocol):
    """Structural type for anything with ``close()``."""

    def close(self) -> None: ...


class Repository[K, V](ABC):
    """Abstract key-value repository. [RULE] PEP 695 syntax for generics."""

    registry: ClassVar[dict[str, type[Repository[Any, Any]]]] = {}

    @abstractmethod
    def get(self, key: K, /) -> V | None:
        """Return the value for ``key``, or None if absent."""

    @abstractmethod
    def put(self, key: K, value: V, /) -> None:
        """Store ``value`` under ``key``, replacing any existing value."""

    def get_or_raise(self, key: K, /) -> V:
        """Return the value for ``key``.

        Raises:
            KeyError: If ``key`` is not present.
        """
        value = self.get(key)
        if value is None:
            raise KeyError(key)
        return value


@final  # [RULE] Mark classes not designed for subclassing as @final.
class InMemoryRepository[K, V](Repository[K, V]):
    """Dict-backed repository, primarily for tests."""

    __slots__ = ("_items",)  # [DECISION] __slots__ on hot/many-instance classes.

    def __init__(self, initial: Mapping[K, V] | None = None) -> None:
        self._items: dict[K, V] = dict(initial or {})

    # --- dunder methods ---------------------------------------------------
    def __repr__(self) -> str:
        # [RULE] __repr__ is unambiguous and, where practical, eval-able.
        return f"{type(self).__name__}({self._items!r})"

    def __len__(self) -> int:
        return len(self._items)

    def __iter__(self) -> Iterator[K]:
        return iter(self._items)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, InMemoryRepository):
            return NotImplemented  # [RULE] Return NotImplemented, never False.
        return self._items == other._items

    __hash__ = None  # type: ignore[assignment]  # mutable => unhashable

    # --- constructors -----------------------------------------------------
    @classmethod
    def from_pairs(cls, pairs: Iterable[tuple[K, V]]) -> Self:
        """Build from ``(key, value)`` pairs. [RULE] Use ``Self`` for return."""
        return cls(dict(pairs))

    @staticmethod
    def _normalize(key: str) -> str:
        # [DECISION] Prefer a module-level function over @staticmethod.
        return key.strip().lower()

    # --- properties -------------------------------------------------------
    @property
    def size(self) -> int:
        """Number of stored items. [RULE] Properties must be O(1) and side-effect free."""
        return len(self._items)

    # --- public API (overrides) ------------------------------------------
    @override  # [RULE] @override on every method that overrides a parent's.
    def get(self, key: K, /) -> V | None:
        return self._items.get(key)

    @override
    def put(self, key: K, value: V, /) -> None:
        self._items[key] = value


# ============================================================================
# 8. FUNCTIONS
# ============================================================================
# [RULE] Functions/methods/variables: snake_case; verbs for actions
#        (``fetch_user``), nouns for pure values, ``is_/has_/can_`` for bools.
# [RULE] Private = single leading underscore. No double-underscore mangling
#        unless you specifically need it.
# [RULE] Public functions are fully type-annotated (params and return).
# [DECISION] Use ``/`` and ``*`` to make API intent explicit:
#            positional-only for obvious first args, keyword-only for flags
#            and anything with more than 2-3 parameters.
# [RULE] Never use a mutable default argument.
# [RULE] Use MISSING (not None) when None is a valid value to pass.


def parse_duration(text: str, /, *, default: float | None | sentinel = MISSING) -> float:
    """Convert ``"1h30m"``-style text to seconds.

    Args:
        text: Duration string made of ``<int><unit>`` tokens
            (units: ms, s, m, h).
        default: Value returned for empty/invalid input. If omitted,
            ``ValueError`` is raised instead.

    Returns:
        Total duration in seconds.

    Raises:
        ValueError: If ``text`` is invalid and no ``default`` was given.

    Example:
        >>> parse_duration("1m30s")
        90.0
    """
    matches = list(_DURATION_RE.finditer(text))
    if not matches or "".join(m.group(0) for m in matches) != text:
        if default is MISSING:
            raise ValueError(f"invalid duration: {text!r}")
        return default  # type: ignore[return-value]
    return sum(int(m["value"]) * SUPPORTED_UNITS[m["unit"]] for m in matches)


# [DECISION] @overload when the return type depends on argument types/values.
@overload
def load(source: str) -> str: ...
@overload
def load(source: bytes) -> bytes: ...
def load(source: str | bytes) -> str | bytes:
    """Return ``source`` unchanged (demonstrates overload layout)."""
    return source


# [RULE] Decorators preserve signatures via ParamSpec + functools.wraps.
def log_calls[**P, R](func: Callable[P, R]) -> Callable[P, R]:
    """Log each call at DEBUG level."""
    logger = logging.getLogger(func.__module__)

    @functools.wraps(func)
    def wrapper(*args: P.args, **kwargs: P.kwargs) -> R:
        logger.debug("calling %s", func.__qualname__)
        return func(*args, **kwargs)

    return wrapper


def chunked[T](items: Iterable[T], size: int) -> Iterator[list[T]]:
    """Yield lists of at most ``size`` items. [RULE] Generators return Iterator/Generator."""
    if size < 1:
        raise ValueError("size must be >= 1")
    batch: list[T] = []
    for item in items:
        batch.append(item)
        if len(batch) == size:
            yield batch
            batch = []
    if batch:
        yield batch


# [DECISION] Lambdas only as short, inline keys/callbacks; never assign to a name.
def sort_by_length(words: Sequence[str]) -> list[str]:
    return sorted(words, key=lambda w: (len(w), w))


# [DECISION] Function length <= ~40 lines; cyclomatic complexity <= 10;
#            early returns preferred over deep nesting.

# ============================================================================
# 9. TYPING CONVENTIONS
# ============================================================================
# [RULE] Builtin generics (list, dict, tuple) and ``X | None``; never
#        typing.List/Optional/Union.
# [RULE] Parameters take abstract types (Sequence, Mapping, Iterable);
#        returns are concrete (list, dict) unless abstraction is intended.
# [RULE] ``Any`` is a last resort and requires a comment explaining why.
# [RULE] ``# type: ignore[code]`` always carries the specific error code and
#        a reason on the same line or the line above.
# [DECISION] Use ``Literal`` for small closed sets; Enum for larger/shared sets.
# [DECISION] TypeForm (3.15) only for APIs that accept type expressions at runtime.

type Mode = Literal["r", "w", "a"]


def describe(mode: Mode) -> str:
    # [RULE] Exhaustive branching ends with assert_never so adding a new
    #        member becomes a type error.
    match mode:
        case "r":
            return "read"
        case "w":
            return "write"
        case "a":
            return "append"
        case _:
            assert_never(mode)


# ============================================================================
# 10. CONTROL FLOW
# ============================================================================
# [RULE] Compare to None/True/False with ``is``; rely on truthiness for
#        sequences (``if not items``) unless None vs empty differs.
# [RULE] No bare ``else`` after return/raise/continue.
# [DECISION] ``match`` for structural dispatch on shape/type; ``if/elif`` for
#            simple value tests. Always include a ``case _`` unless exhaustive.


def handle(event: Mapping[str, Any] | Sequence[Any] | int) -> str:
    match event:
        case {"type": "click", "pos": (x, y)}:
            return f"click at {x},{y}"
        case {"type": str(kind)}:
            return f"event {kind}"
        case [first, *rest]:
            return f"seq starting {first} (+{len(rest)})"
        case int(n) if n < 0:  # guard
            return "negative"
        case _:
            return "unknown"


def word_stats(lines: Iterable[str]) -> dict[str, int]:
    # [DECISION] Comprehensions: one ``for`` and at most one ``if``; anything
    #            more complex becomes a loop. Never use them for side effects.
    counts: defaultdict[str, int] = defaultdict(int)
    for line in lines:
        for word in line.split():
            counts[word.lower()] += 1
    lengths = {w: len(w) for w in counts if len(w) > 3}
    # [DECISION] Walrus only to avoid recomputation in a condition.
    if (total := sum(counts.values())) > 1000:
        logging.getLogger(__name__).info("large input: %d words", total)
    return {**counts, **lengths}


# ============================================================================
# 11. STRINGS
# ============================================================================
# [RULE] f-strings for formatting; ``%s`` lazy args ONLY in logging calls;
#        str.format only for templates stored outside code.
# [RULE] Use ``!r`` for values that may contain whitespace/be empty in errors.
# [RULE] Always pass encoding= explicitly to open()/read_text()/write_text()
#        even though UTF-8 is the default since 3.15 (clarity, portability).
# [DECISION] t-strings (PEP 750, 3.14+) for SQL/HTML/shell building where the
#            consumer escapes interpolations; never f-strings for those.
# [RULE] Join with ``"".join``; no += in loops.
# [RULE] Regexes are precompiled raw strings (see _DURATION_RE); use verbose
#        mode (re.VERBOSE) once a pattern exceeds ~40 characters.


def greeting(name: str, *, width: int = 20) -> str:
    return f"{name!r:>{width}} | {len(name):03d} | {1234567.891:,.2f}"


# ============================================================================
# 12. ERRORS AND EXCEPTIONS
# ============================================================================
# [RULE] Define a project base exception; all custom errors derive from it.
# [RULE] Catch the narrowest type; never bare ``except:``; ``except Exception``
#        only at process/task boundaries, and it must log or re-raise.
# [RULE] ``raise NewError(...) from err`` to chain; ``from None`` only to
#        deliberately hide an implementation detail.
# [RULE] Messages: lowercase, no trailing period, include offending value.
# [DECISION] Prefer exceptions over error return codes; never return None to
#            signal failure unless the signature says ``X | None``.
# [DECISION] LBYL vs EAFP: EAFP default; LBYL when the check is cheap and the
#            failure would be destructive.


class AppError(Exception):
    """Base class for all project exceptions."""


class ConfigError(AppError):
    """Raised for invalid or missing configuration."""

    def __init__(self, key: str, reason: str) -> None:
        super().__init__(f"config key {key!r}: {reason}")
        self.key = key
        self.reason = reason


def read_config(path: Path) -> dict[str, Any]:
    try:
        raw = path.read_text(encoding="utf-8")
        return json.loads(raw)
    except FileNotFoundError as err:
        raise ConfigError(str(path), "file not found") from err
    except json.JSONDecodeError as err:
        raise ConfigError(str(path), f"invalid JSON at line {err.lineno}") from err
    # [RULE] No ``finally`` for resources that have a context manager.


async def run_all(tasks: Sequence[Callable[[], Awaitable[None]]]) -> None:
    # [RULE] Use ``except*`` only with TaskGroup/ExceptionGroup.
    try:
        async with asyncio.TaskGroup() as group:
            for task in tasks:
                group.create_task(task())
    except* ConnectionError as eg:
        logging.getLogger(__name__).error("connection failures: %d", len(eg.exceptions))
        raise


# ============================================================================
# 13. RESOURCES, CONTEXT MANAGERS, PATHS, I/O
# ============================================================================
# [RULE] pathlib.Path over os.path; ``Path`` in signatures, not ``str``.
# [RULE] Always use ``with`` for files, locks, connections, temp dirs.
# [DECISION] @contextlib.contextmanager for simple cases; class with
#            __enter__/__exit__ when state or reuse is needed.
# [RULE] subprocess: list args, ``check=True``, never ``shell=True`` with
#        untrusted input, always set timeout=.


@contextlib.contextmanager
def working_directory(path: Path) -> Iterator[Path]:
    """Temporarily change the working directory."""
    import os  # [DECISION] Function-local imports only to break cycles/cost.

    previous = Path.cwd()
    os.chdir(path)
    try:
        yield path
    finally:
        os.chdir(previous)


def git_head(repo: Path) -> str:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=repo,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=True,
        timeout=DEFAULT_TIMEOUT_S,
    )
    return result.stdout.strip()


# ============================================================================
# 14. CONCURRENCY
# ============================================================================
# [DECISION] asyncio for I/O-bound; ProcessPoolExecutor for CPU-bound;
#            threads only for blocking libraries. Free-threaded build
#            (python3.15t): document any thread-safety assumptions.
# [RULE] Async functions are named like sync ones (no ``_async`` suffix)
#        unless a sync twin exists in the same module.
# [RULE] Structured concurrency (TaskGroup, timeout()) over bare
#        create_task/gather; never fire-and-forget without keeping a reference.
# [RULE] Never block the event loop; wrap blocking calls in asyncio.to_thread.
# [RULE] Shared mutable state is guarded by a lock or avoided entirely.


async def fetch_all(urls: Sequence[str], *, limit: int = 10) -> list[bytes]:
    semaphore = asyncio.Semaphore(limit)
    results: list[bytes] = [b""] * len(urls)

    async def one(index: int, url: str) -> None:
        async with semaphore:
            async with asyncio.timeout(DEFAULT_TIMEOUT_S):
                results[index] = await asyncio.to_thread(_blocking_get, url)

    async with asyncio.TaskGroup() as group:
        for i, url in enumerate(urls):
            group.create_task(one(i, url))
    return results


def _blocking_get(url: str) -> bytes:
    raise NotImplementedError(url)  # placeholder for a real client


async def ticker(count: int) -> AsyncIterator[int]:
    """Async generator example."""
    for i in range(count):
        yield i
        await asyncio.sleep(0)


# ============================================================================
# 15. LOGGING, WARNINGS, DEPRECATION
# ============================================================================
# [RULE] One module-level ``logger = logging.getLogger(__name__)``; never
#        configure logging (handlers/levels) in library code.
# [RULE] Lazy %-style args in log calls; no f-strings in logging.
# [RULE] ``logger.exception`` inside except blocks; no ``print`` in library
#        code (CLI output only, in main()).
# [RULE] Never log secrets/PII.
# [DECISION] Structured logging via ``extra={...}`` (or structlog).
# [RULE] Deprecate with warnings.deprecated, giving the removal version.

logger = logging.getLogger(__name__)


@warnings.deprecated("use parse_duration() instead; removal in 2.0")
def parse_time(text: str) -> float:
    return parse_duration(text)


def risky(path: Path) -> None:
    try:
        path.unlink()
    except OSError:
        logger.exception("could not delete %s", path)


# ============================================================================
# 16. COMMENTS, DOCSTRINGS, TODOS
# ============================================================================
# [RULE] Comments explain WHY, not WHAT. Full sentences, capitalized for
#        block comments; inline comments need two spaces before ``#``.
# [RULE] Docstrings on every public module/class/function/method;
#        Google style (Args/Returns/Raises/Yields/Example); first line is an
#        imperative one-sentence summary ending with a period.
# [DECISION] Private helpers need a docstring only if non-obvious.
# [RULE] Don't restate type hints in docstrings.
# [RULE] No commented-out code; delete it (git remembers).
# [RULE] TODO format: ``# TODO(owner, 2026-12-01): description (#issue)``.
# [DECISION] Section banners (like this file) allowed only in files > 300 lines.

# TODO(alice, 2026-12-31): replace placeholder HTTP client (#123)

# ============================================================================
# 17. TESTING CONVENTIONS (illustrated; real tests live in tests/)
# ============================================================================
# [DECISION] pytest; files ``tests/test_<module>.py``; functions
#            ``test_<unit>_<scenario>_<expected>``.
# [RULE] Arrange / Act / Assert separated by blank lines; one behavior per test.
# [RULE] ``pytest.mark.parametrize`` over loops; fixtures over setup code;
#        no logic (if/for) in tests; no network or real clock in unit tests.
# [RULE] Tests are annotated lightly (``-> None``), docstrings optional.
#
#   @pytest.mark.parametrize(
#       ("text", "expected"),
#       [("1s", 1.0), ("2m", 120.0), ("1m30s", 90.0)],
#   )
#   def test_parse_duration_valid_text_returns_seconds(text: str, expected: float) -> None:
#       assert parse_duration(text) == expected
#
#   def test_parse_duration_invalid_text_raises() -> None:
#       with pytest.raises(ValueError, match="invalid duration"):
#           parse_duration("abc")

# ============================================================================
# 18. ENTRY POINTS
# ============================================================================
# [RULE] ``main(argv)`` takes args and returns an int exit code; side effects
#        live under the ``__main__`` guard only.
# [DECISION] argparse (stdlib) | click | typer for CLIs.


def main(argv: Sequence[str] | None = None) -> int:
    """Run the command-line interface.

    Args:
        argv: Arguments excluding the program name; defaults to ``sys.argv[1:]``.

    Returns:
        Process exit code (0 on success).
    """
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("duration", help="duration such as 1h30m")
    args = parser.parse_args(argv)

    try:
        print(parse_duration(args.duration))
    except ValueError as err:
        print(f"error: {err}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())