"""Sparse relative-order merging for Niagara execution stacks."""

from __future__ import annotations

import heapq


def merge_order(base: list, ours: list, theirs: list) -> tuple[list, str | None]:
    if ours == base:
        return list(theirs), None
    if theirs == base or ours == theirs:
        return list(ours), None
    alive = (set(ours) | set(theirs)) - ((set(base) - set(ours)) | (set(base) - set(theirs)))
    sequences = [[item for item in seq if item in alive] for seq in (base, ours, theirs)]
    edges = [set(zip(seq, seq[1:])) for seq in sequences]
    constraints = (edges[0] & edges[1] & edges[2]) | (edges[1] - edges[0]) | (edges[2] - edges[0])
    outgoing = dict((item, set()) for item in alive)
    indegree = dict.fromkeys(alive, 0)
    for left, right in constraints:
        outgoing[left].add(right)
        indegree[right] += 1
    positions = dict((item, index) for index, item in enumerate(base))
    ready = [(positions.get(item, len(base)), item) for item in alive if not indegree[item]]
    heapq.heapify(ready)
    ordered, ambiguous = [], False
    while ready:
        if len(ready) > 1:
            new = [item for _, item in ready if item not in positions]
            if len(new) > 1:
                ambiguous = True
        _, item = heapq.heappop(ready)
        ordered.append(item)
        for target in outgoing[item]:
            indegree[target] -= 1
            if not indegree[target]:
                heapq.heappush(ready, (positions.get(target, len(base)), target))
    if len(ordered) != len(alive):
        return list(ours), "relative order constraints form a cycle"
    if ambiguous:
        return ordered, "concurrent insertions have no defined execution order"
    return ordered, None
