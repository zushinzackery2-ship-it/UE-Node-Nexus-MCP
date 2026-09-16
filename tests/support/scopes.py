"""A work-scope double for facade tests whose editor transport is also a double."""

from contextlib import nullcontext


class Scope:
    def __init__(self, *_args, **_kwargs):
        self.released = False

    def activate(self):
        return nullcontext(self)

    def release(self):
        self.released = True
