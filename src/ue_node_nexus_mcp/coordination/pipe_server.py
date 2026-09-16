"""Bounded Windows pipe workers using the bridge's existing JSON framing."""

from __future__ import annotations

import logging
import struct
import threading

from ..errors import BridgeError
from ..transport import _win, decode_body, encode_frame, named_pipe_transport

LOG = logging.getLogger(__name__)
MAX_CONTROL_BYTES = 1024 * 1024


class PipeServer:
    def __init__(self, address: str, handler, workers: int = 4) -> None:
        self.address, self.handler = address, handler
        self.stopped = threading.Event()
        self.threads = []
        self.ready = threading.Event()
        self.worker_count = workers
        self.failure = None

    def start(self) -> PipeServer:
        for number in range(self.worker_count):
            thread = threading.Thread(target=self._worker, name=f"nexus-control-{number}", daemon=True)
            self.threads.append(thread)
            thread.start()
        if not self.ready.wait(5):
            self.close()
            raise BridgeError(f"control listener failed to start: {self.failure}")
        return self

    def close(self) -> None:
        self.stopped.set()
        for thread in self.threads:
            thread.join(5)
        if any(thread.is_alive() for thread in self.threads):
            raise RuntimeError("control worker failed to stop")

    def _api(self):
        win = _win()
        c, w = win.ctypes, win.wintypes
        api = c.WinDLL("kernel32", use_last_error=True)
        api.CreateNamedPipeW.argtypes = [w.LPCWSTR] + [w.DWORD] * 6 + [w.LPVOID]
        api.CreateNamedPipeW.restype = w.HANDLE
        api.ConnectNamedPipe.argtypes = [w.HANDLE, w.LPVOID]
        api.ConnectNamedPipe.restype = w.BOOL
        api.GetNamedPipeClientProcessId.argtypes = [w.HANDLE, c.POINTER(w.ULONG)]
        api.GetNamedPipeClientProcessId.restype = w.BOOL
        return win, api

    def _security(self, win):
        from ..instances.identity.processes import user_identity
        c, w = win.ctypes, win.wintypes

        class Attributes(c.Structure):
            _fields_ = [("length", w.DWORD), ("descriptor", w.LPVOID), ("inherit", w.BOOL)]

        advapi = c.WinDLL("advapi32", use_last_error=True)
        convert = advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW
        convert.argtypes = [w.LPCWSTR, w.DWORD, c.POINTER(w.LPVOID), w.LPVOID]
        convert.restype = w.BOOL
        descriptor = w.LPVOID()
        sddl = f"D:P(A;;GA;;;SY)(A;;GA;;;{user_identity()})S:(ML;;NW;;;ME)"
        if not convert(sddl, 1, c.byref(descriptor), None):
            raise c.WinError(c.get_last_error())
        return Attributes(c.sizeof(Attributes), descriptor, False)

    def _worker(self) -> None:
        win, api = self._api()
        security = None
        try:
            security = self._security(win)
            while not self.stopped.is_set():
                pipe = api.CreateNamedPipeW(self.address, 3 | 0x40000000, 0x8, 255, 65536, 65536, 0,
                                            win.ctypes.byref(security))
                if pipe == win.ctypes.c_void_p(-1).value:
                    raise win.ctypes.WinError(win.ctypes.get_last_error())
                try:
                    self.ready.set()
                    if self._accept(win, api, pipe):
                        self._serve(win, api, pipe)
                finally:
                    win.CloseHandle(pipe)
        except BaseException as exc:
            self.failure = repr(exc)
            LOG.exception("control listener failed")
            self.stopped.set()
        finally:
            if security is not None:
                from ..instances.identity.windows import kernel
                kernel().LocalFree(security.descriptor)

    def _accept(self, win, api, pipe) -> bool:
        ov = win.OVERLAPPED()
        ov.hEvent = win.CreateEventW(None, True, False, None)
        try:
            if api.ConnectNamedPipe(pipe, win.ctypes.byref(ov)):
                return True
            error = win.ctypes.get_last_error()
            if error == 535:
                return True
            if error != win.ERROR_IO_PENDING:
                raise win.ctypes.WinError(error)
            while not self.stopped.is_set():
                if win.WaitForSingleObject(ov.hEvent, 100) == 0:
                    transferred = win.wintypes.DWORD()
                    return bool(win.GetOverlappedResult(pipe, win.ctypes.byref(ov), win.ctypes.byref(transferred), False))
            win.CancelIoEx(pipe, win.ctypes.byref(ov))
            transferred = win.wintypes.DWORD()
            win.GetOverlappedResult(pipe, win.ctypes.byref(ov), win.ctypes.byref(transferred), True)
            return False
        finally:
            win.CloseHandle(ov.hEvent)

    def _serve(self, win, api, pipe) -> None:
        transport = named_pipe_transport
        try:
            length = struct.unpack("<I", transport._read_exact(win, pipe, 4, 2000))[0]
            if not 0 < length <= MAX_CONTROL_BYTES:
                return
            request = decode_body(transport._read_exact(win, pipe, length, 2000))
            pid = win.wintypes.ULONG()
            if not api.GetNamedPipeClientProcessId(pipe, win.ctypes.byref(pid)):
                raise win.ctypes.WinError(win.ctypes.get_last_error())
            response = self.handler(request, int(pid.value))
            frame = encode_frame(response)
            if len(frame) > MAX_CONTROL_BYTES:
                frame = encode_frame(dict(ok=False, error=dict(code="control_response_too_large", message="narrow the query")))
            transport._write_all(win, pipe, frame, 2000)
            # Wait for client EOF after it has consumed the response, bounded on shutdown.
            try:
                transport._read_exact(win, pipe, 1, 1000)
            except BridgeError:
                pass
        except (BridgeError, OSError, ValueError):
            LOG.debug("control connection ended", exc_info=True)
