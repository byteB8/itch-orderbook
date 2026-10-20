"""Independent check of the C++ order books, written separately in Python from the ITCH 5.0 specification.

Replays an ITCH file with a plain dict of orders, then writes, for every instrument with orders on the
book, the same summary that `replay --summary` writes: price levels per side, best bid and ask, shares and
orders per side. With --compare, reports every row that differs from the C++ summary.

    python3 scripts/check_books.py <itch file> <out.csv> [--compare cpp.csv]
"""
from __future__ import annotations

import csv
import mmap
import struct
import sys
from collections import defaultdict

LENGTH = struct.Struct(">H")
LOCATE = struct.Struct(">H")
ADD = struct.Struct(">QcI8xI")      # reference, side, shares, stock (skipped), price
REF_SHARES = struct.Struct(">QI")   # E, C, X: reference, shares
REF = struct.Struct(">Q")           # D: reference
REPLACE = struct.Struct(">QQII")    # U: old reference, new reference, shares, price
COLUMNS = ["locate", "symbol", "bid_levels", "ask_levels", "best_bid", "best_ask", "bid_shares", "ask_shares",
           "bid_orders", "ask_orders"]


def replay(path: str) -> tuple[dict, dict, dict]:
    orders: dict[int, list] = {}   # reference -> [locate, side, price, shares]
    symbols: dict[int, str] = {}
    problems = {"messages": 0, "unknown orders": 0, "over-executed": 0}

    def reduce(ref: int, shares: int) -> None:
        o = orders.get(ref)
        if o is None:
            problems["unknown orders"] += 1
        elif shares >= o[3]:
            problems["over-executed"] += shares > o[3]
            del orders[ref]
        else:
            o[3] -= shares

    with open(path, "rb") as f, mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ) as mm:
        pos, end = 0, len(mm)
        while pos + 2 <= end:
            (length,) = LENGTH.unpack_from(mm, pos)
            m = pos + 2
            if m + length > end:
                break                      # the last message was cut by the partial download
            pos = m + length
            problems["messages"] += 1
            kind = mm[m]
            if kind in (65, 70):           # A, F
                ref, side, shares, price = ADD.unpack_from(mm, m + 11)
                orders[ref] = [LOCATE.unpack_from(mm, m + 1)[0], side, price, shares]
            elif kind in (69, 67, 88):     # E, C, X
                reduce(*REF_SHARES.unpack_from(mm, m + 11))
            elif kind == 68:               # D
                if orders.pop(REF.unpack_from(mm, m + 11)[0], None) is None:
                    problems["unknown orders"] += 1
            elif kind == 85:               # U
                old, new, shares, price = REPLACE.unpack_from(mm, m + 11)
                o = orders.pop(old, None)
                if o is None:
                    problems["unknown orders"] += 1
                else:
                    orders[new] = [o[0], o[1], price, shares]
            elif kind == 82:               # R
                symbols[LOCATE.unpack_from(mm, m + 1)[0]] = mm[m + 11:m + 19].decode("ascii").rstrip()
    return orders, symbols, problems


def summarise(orders: dict, symbols: dict) -> list[dict]:
    levels: dict[tuple, dict] = defaultdict(lambda: defaultdict(lambda: [0, 0]))  # (locate, side) -> price -> [shares, orders]
    for locate, side, price, shares in orders.values():
        level = levels[(locate, side)][price]
        level[0] += shares
        level[1] += 1
    rows = []
    for locate in sorted({key[0] for key in levels}):
        bids, asks = levels.get((locate, b"B"), {}), levels.get((locate, b"S"), {})
        rows.append({
            "locate": locate, "symbol": symbols.get(locate, ""),
            "bid_levels": len(bids), "ask_levels": len(asks),
            "best_bid": max(bids) if bids else 0, "best_ask": min(asks) if asks else 0,
            "bid_shares": sum(v[0] for v in bids.values()), "ask_shares": sum(v[0] for v in asks.values()),
            "bid_orders": sum(v[1] for v in bids.values()), "ask_orders": sum(v[1] for v in asks.values()),
        })
    return rows


def main() -> int:
    if len(sys.argv) not in (3, 5):
        print(__doc__)
        return 2
    orders, symbols, problems = replay(sys.argv[1])
    rows = summarise(orders, symbols)
    with open(sys.argv[2], "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=COLUMNS)
        writer.writeheader()
        writer.writerows(rows)
    print(", ".join(f"{k} {v}" for k, v in problems.items()) + f", orders on the book {len(orders)}, "
          f"instruments with orders {len(rows)}")
    if len(sys.argv) == 5 and sys.argv[3] == "--compare":
        with open(sys.argv[4], newline="") as f:
            theirs = {int(r["locate"]): r for r in csv.DictReader(f)}
        ours = {r["locate"]: {k: str(v) for k, v in r.items()} for r in rows}
        differ = sorted(k for k in ours.keys() | theirs.keys() if ours.get(k) != theirs.get(k))
        for k in differ[:10]:
            print("differs:", ours.get(k), theirs.get(k))
        print(f"compared {len(ours.keys() | theirs.keys())} instruments: {len(differ)} differ")
        return 1 if differ else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
