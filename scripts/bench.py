#!/usr/bin/env python3
"""Run Aurora against EPD test suites and compare UCI best moves."""

from __future__ import annotations

import argparse
import queue
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ENGINE = ROOT / "build" / "bin" / "release" / "aurora_chess_app.exe"
DEFAULT_MOVETIME_MS = 20_000


@dataclass(frozen=True)
class Position:
    index: int
    fen: str
    best_moves: set[str]
    raw_best_moves: list[str]
    line: str


@dataclass(frozen=True)
class Result:
    position: Position
    bestmove: str
    score: str | None
    nodes: int | None
    elapsed_ms: int
    passed: bool
    engine_lines: list[str]


MOVE_RE = re.compile(
    r"^(?P<piece>[KQRBN])?(?P<from>[a-h][1-8])[-x](?P<to>[a-h][1-8])(?P<promotion>[QRBN])?$"
)


def normalize_epd_move(text: str, side_to_move: str) -> str:
    move = text.strip().replace("0", "O")
    move = move.rstrip("+#?!")

    if move in {"O-O", "OO"}:
        return "e1g1" if side_to_move == "w" else "e8g8"
    if move in {"O-O-O", "OOO"}:
        return "e1c1" if side_to_move == "w" else "e8c8"

    match = MOVE_RE.match(move)
    if not match:
        raise ValueError(f"unsupported bm move notation: {text!r}")

    promotion = match.group("promotion")
    return match.group("from") + match.group("to") + (promotion.lower() if promotion else "")


def parse_epd_line(index: int, line: str) -> Position:
    chunks = line.split()
    if len(chunks) < 4:
        raise ValueError("EPD line must start with four FEN fields")

    fen_fields = chunks[:4]
    fen = " ".join(fen_fields + ["0", "1"])
    side_to_move = fen_fields[1]

    bm_match = re.search(r"\bbm\s+([^;]+)", line)
    raw_best_moves: list[str] = []
    best_moves: set[str] = set()
    if bm_match:
        raw_best_moves = [move for move in bm_match.group(1).split() if move != "bm"]
        best_moves = {normalize_epd_move(move, side_to_move) for move in raw_best_moves}

    return Position(index=index, fen=fen, best_moves=best_moves, raw_best_moves=raw_best_moves, line=line)


def load_epd(path: Path, limit: int | None) -> list[Position]:
    positions: list[Position] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            positions.append(parse_epd_line(line_number, line))
            if limit is not None and len(positions) >= limit:
                break
    return positions


class UciEngine:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.lines: queue.Queue[str | None] = queue.Queue()
        self.process = subprocess.Popen(
            [str(path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()

    def _read_output(self) -> None:
        if self.process.stdout is None:
            self.lines.put(None)
            return
        try:
            for line in self.process.stdout:
                self.lines.put(line.strip())
        finally:
            self.lines.put(None)

    def close(self) -> None:
        if self.process.poll() is not None:
            return
        try:
            self.send("quit")
        except (BrokenPipeError, OSError):
            pass
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            try:
                self.process.terminate()
                self.process.wait(timeout=2)
            except (OSError, subprocess.TimeoutExpired):
                try:
                    self.process.kill()
                    self.process.wait(timeout=2)
                except OSError:
                    pass
        finally:
            if self.process.stdin is not None:
                try:
                    self.process.stdin.close()
                except OSError:
                    pass

    def send(self, command: str) -> None:
        if self.process.stdin is None:
            raise RuntimeError("engine stdin is closed")
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def read_until(self, token: str, timeout: float = 10.0) -> list[str]:
        deadline = time.monotonic() + timeout
        lines: list[str] = []
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            try:
                line = self.lines.get(timeout=max(0.001, remaining))
            except queue.Empty:
                break
            if line is None:
                raise RuntimeError("engine exited unexpectedly")
            lines.append(line)
            if line == token or line.startswith(token + " "):
                return lines
        raise TimeoutError(f"timed out waiting for {token!r}")

    def initialize(self) -> None:
        self.send("uci")
        self.read_until("uciok")
        self.send("isready")
        self.read_until("readyok")

    def search(self, fen: str, go_command: str, timeout: float) -> tuple[str, str | None, int | None, list[str], bool]:
        self.send(f"position fen {fen}")
        self.send(go_command)

        timed_out = False
        try:
            lines = self.read_until("bestmove", timeout=timeout)
        except TimeoutError:
            timed_out = True
            self.send("stop")
            try:
                lines = self.read_until("bestmove", timeout=5.0)
            except TimeoutError:
                lines = ["info string bench timeout waiting for bestmove after stop", "bestmove 0000"]

        bestmove = "0000"
        score: str | None = None
        nodes: int | None = None
        for line in lines:
            if line.startswith("info "):
                score_match = re.search(r"\bscore\s+(cp|mate)\s+(-?\d+)", line)
                if score_match:
                    score = f"{score_match.group(1)} {score_match.group(2)}"
                nodes_match = re.search(r"\bnodes\s+(\d+)", line)
                if nodes_match:
                    nodes = int(nodes_match.group(1))
            elif line.startswith("bestmove "):
                parts = line.split()
                if len(parts) >= 2:
                    bestmove = parts[1]
        return bestmove, score, nodes, lines, timed_out


def print_result(result: Result, ordinal: int, total: int, show_engine_log: bool) -> None:
    expected = ",".join(sorted(result.position.best_moves)) if result.position.best_moves else "(none)"
    marker = "ok" if result.passed else "FAIL"
    score_text = f" score={result.score}" if result.score is not None else ""
    nodes_text = f" nodes={result.nodes}" if result.nodes is not None else ""
    print(
        f"{marker:4} {ordinal:4}/{total} line={result.position.index} "
        f"best={result.bestmove} expected={expected}{score_text}{nodes_text} time={result.elapsed_ms}ms"
    )
    if show_engine_log:
        for line in result.engine_lines:
            print(f"     engine: {line}")


def run_bench(args: argparse.Namespace) -> int:
    epd_path = args.epd.resolve()
    engine_path = args.engine.resolve()
    if not epd_path.exists():
        print(f"error: EPD file not found: {epd_path}", file=sys.stderr)
        return 2
    if not engine_path.exists():
        print(f"error: engine not found: {engine_path}", file=sys.stderr)
        return 2

    positions = load_epd(epd_path, args.limit)
    if not positions:
        print(f"error: no positions found in {epd_path}", file=sys.stderr)
        return 2

    engine = UciEngine(engine_path)
    results: list[Result] = []
    total_nodes = 0
    go_command = search_command(args)
    started = time.perf_counter()
    try:
        engine.initialize()
        for ordinal, position in enumerate(positions, start=1):
            search_started = time.perf_counter()
            bestmove, score, nodes, engine_lines, timed_out = engine.search(position.fen, go_command, args.timeout)
            elapsed_ms = int((time.perf_counter() - search_started) * 1000)
            passed = not position.best_moves or bestmove in position.best_moves
            if timed_out:
                passed = False
            if nodes is not None:
                total_nodes += nodes
            if timed_out:
                engine_lines.insert(0, f"info string bench sent stop after {args.timeout:.1f}s")
            result = Result(position, bestmove, score, nodes, elapsed_ms, passed, engine_lines)
            results.append(result)

            if not args.quiet or args.verbose or not passed:
                print_result(result, ordinal, len(positions), args.engine_log)

            if args.fail_fast and not passed:
                break
    finally:
        engine.close()

    elapsed = time.perf_counter() - started
    passed_count = sum(1 for result in results if result.passed)
    failed_count = len(results) - passed_count
    nps = int(total_nodes / elapsed) if elapsed > 0 and total_nodes > 0 else 0

    print()
    print(f"EPD:      {epd_path}")
    print(f"Engine:   {engine_path}")
    print(f"Mode:     {go_command}")
    print(f"Result:   {passed_count}/{len(results)} passed, {failed_count} failed")
    print(f"Nodes:    {total_nodes}")
    print(f"NPS:      {nps}")
    print(f"Elapsed:  {elapsed:.2f}s")
    return 0 if failed_count == 0 else 1


def search_command(args: argparse.Namespace) -> str:
    if args.go:
        return "go " + args.go.strip()
    if args.depth is not None:
        return f"go depth {args.depth}"
    return f"go movetime {args.movetime}"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Bench Aurora on an EPD suite.")
    parser.add_argument("epd", type=Path, help="EPD file to run")
    parser.add_argument("--engine", type=Path, default=DEFAULT_ENGINE, help="path to aurora_chess_app executable")
    parser.add_argument("--depth", type=int, default=None, help="optional fixed search depth")
    parser.add_argument(
        "--movetime",
        type=int,
        default=DEFAULT_MOVETIME_MS,
        help=f"fixed time per position in milliseconds, default {DEFAULT_MOVETIME_MS}",
    )
    parser.add_argument("--go", default=None, help="raw arguments after 'go', for example: 'nodes 100000'")
    parser.add_argument("--limit", type=int, default=None, help="maximum number of positions to run")
    parser.add_argument(
        "--timeout",
        type=float,
        default=None,
        help="seconds to wait before sending stop, default is movetime plus five seconds",
    )
    parser.add_argument("--verbose", action="store_true", help="print every position even when --quiet is set")
    parser.add_argument("--quiet", action="store_true", help="print only failures and the final summary")
    parser.add_argument(
        "--engine-log",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="print raw UCI info/bestmove lines for each searched position",
    )
    parser.add_argument("--fail-fast", action="store_true", help="stop at the first failed position")
    args = parser.parse_args()
    if args.depth is not None and args.depth < 1:
        parser.error("--depth must be at least 1")
    if args.movetime < 1:
        parser.error("--movetime must be at least 1")
    if args.timeout is None:
        args.timeout = max(1.0, args.movetime / 1000.0 + 5.0)
    return args


if __name__ == "__main__":
    raise SystemExit(run_bench(parse_args()))
