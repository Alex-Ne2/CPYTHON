__all__ = (
    'Stream', 'StreamMode',
    'open_connection', 'start_server',
    'connect',
    'StreamServer')

import enum
import socket
import sys
import warnings
import weakref

if hasattr(socket, 'AF_UNIX'):
    __all__ += ('open_unix_connection', 'start_unix_server',
                'connect_unix',
                'UnixStreamServer')

from . import coroutines
from . import events
from . import exceptions
from . import format_helpers
from . import protocols
from .log import logger
from .tasks import sleep, wait


_DEFAULT_LIMIT = 2 ** 16  # 64 KiB


class StreamMode(enum.Flag):
    READ = enum.auto()
    WRITE = enum.auto()
    READWRITE = READ | WRITE

    def _check_read(self):
        if not self & self.READ:
            raise RuntimeError("The stream is write-only")

    def _check_write(self):
        if not self & self.WRITE:
            raise RuntimeError("The stream is read-only")


async def connect(host=None, port=None, *,
                  limit=_DEFAULT_LIMIT,
                  ssl=None, family=0, proto=0,
                  flags=0, sock=None, local_addr=None,
                  server_hostname=None,
                  ssl_handshake_timeout=None,
                  happy_eyeballs_delay=None, interleave=None):
    loop = events.get_running_loop()
    stream = Stream(mode=StreamMode.READWRITE,
                    limit=limit,
                    loop=loop,
                    _asyncio_internal=True)
    await loop.create_connection(
        lambda: _StreamProtocol(stream, loop=loop,
                                _asyncio_internal=True),
        host, port,
        ssl=ssl, family=family, proto=proto,
        flags=flags, sock=sock, local_addr=local_addr,
        server_hostname=server_hostname,
        ssl_handshake_timeout=ssl_handshake_timeout,
        happy_eyeballs_delay=happy_eyeballs_delay, interleave=interleave)
    return stream


async def open_connection(host=None, port=None, *,
                          loop=None, limit=_DEFAULT_LIMIT, **kwds):
    """A wrapper for create_connection() returning a (reader, writer) pair.

    The reader returned is a StreamReader instance; the writer is a
    StreamWriter instance.

    The arguments are all the usual arguments to create_connection()
    except protocol_factory; most common are positional host and port,
    with various optional keyword arguments following.

    Additional optional keyword arguments are loop (to set the event loop
    instance to use) and limit (to set the buffer limit passed to the
    StreamReader).

    (If you want to customize the StreamReader and/or
    StreamReaderProtocol classes, just copy the code -- there's
    really nothing special here except some convenience.)
    """
    if loop is None:
        loop = events.get_event_loop()
    stream = Stream(mode=StreamMode.READWRITE,
                    limit=limit,
                    loop=loop,
                    _asyncio_internal=True)
    await loop.create_connection(
        lambda: _StreamProtocol(stream, loop=loop,
                                _asyncio_internal=True),
        host, port, **kwds)
    return stream, stream


async def start_server(client_connected_cb, host=None, port=None, *,
                       loop=None, limit=_DEFAULT_LIMIT, **kwds):
    """Start a socket server, call back for each client connected.

    The first parameter, `client_connected_cb`, takes two parameters:
    client_reader, client_writer.  client_reader is a StreamReader
    object, while client_writer is a StreamWriter object.  This
    parameter can either be a plain callback function or a coroutine;
    if it is a coroutine, it will be automatically converted into a
    Task.

    The rest of the arguments are all the usual arguments to
    loop.create_server() except protocol_factory; most common are
    positional host and port, with various optional keyword arguments
    following.  The return value is the same as loop.create_server().

    Additional optional keyword arguments are loop (to set the event loop
    instance to use) and limit (to set the buffer limit passed to the
    StreamReader).

    The return value is the same as loop.create_server(), i.e. a
    Server object which can be used to stop the service.
    """
    if loop is None:
        loop = events.get_event_loop()

    def factory():
        protocol = _LegacyServerStreamProtocol(limit,
                                               client_connected_cb,
                                               loop=loop,
                                               _asyncio_internal=True)
        return protocol

    return await loop.create_server(factory, host, port, **kwds)


class _BaseStreamServer:
    # TODO: API for enumerating open server streams
    # TODO: add __repr__

    # Design note.
    # StreamServer and UnixStreamServer are exposed as FINAL classes, not function
    # factories.
    # async with serve(host, port) as server:
    #      server.start_serving()
    # looks ugly.

    def __init__(self, client_connected_cb,
                 limit=_DEFAULT_LIMIT,
                 shutdown_timeout=60,
                 _asyncio_internal=False):
        if not _asyncio_internal:
            raise RuntimeError("_ServerStream is a private asyncio class")
        self._client_connected_cb = client_connected_cb
        self._limit = limit
        self._loop = events.get_running_loop()
        self._low_server = None
        self._streams = {}
        self._shutdown_timeout = shutdown_timeout

    async def bind(self):
        if self._low_server is not None:
            return
        self._low_server = await self._bind()

    def is_bound(self):
        # TODO: make is_bound and is_serving properties?
        return self._low_server is not None

    def served_names(self):
        # The property name is questionable
        # Also, should it be a property?
        # API consistency does matter
        # I don't want to expose plain socket.socket objects as low-level
        # asyncio.Server does but exposing served IP addresses or unix paths
        # is useful
        #
        # multiple value for socket bound to both IPv4 and IPv6 families
        if self._low_server is None:
            return []
        return [sock.getsockname() for sock in self._low_server.sockets]

    def is_serving(self):
        # TODO: make is_bound and is_serving properties?
        if self._low_server is None:
            return False
        return self._low_server.is_serving()

    async def start_serving(self):
        await self.bind()
        await self._low_server.start_serving()

    async def serve_forever(self):
        await self.start_serving()
        await self._low_server.serve_forever()

    async def close(self):
        if self._low_server is None:
            return
        self._low_server.close()
        streams = list(self._streams.keys())
        tasks = list(self._streams.values())
        if tasks:
            await wait([stream.close() for stream in streams])
        await self._low_server.wait_closed()
        self._low_server = None
        await self._shutdown_active_tasks(tasks)

    async def abort(self):
        if self._low_server is None:
            return
        self._low_server.close()
        streams = list(self._streams.keys())
        tasks = list(self._streams.values())
        if streams:
            await wait([stream.abort() for stream in streams])
        await self._low_server.wait_closed()
        self._low_server = None
        await self._shutdown_active_tasks(tasks)

    async def __aenter__(self):
        await self.bind()
        return self

    async def __aexit__(self, exc_type, exc_value, exc_tb):
        await self.close()

    def __init_subclass__(cls):
        if not cls.__module__.startswith('asyncio.'):
            raise TypeError("Stream server classes are final, don't inherit from them")

    def _attach(self, stream, task):
        self._streams[stream] = task

    def _detach(self, stream, task):
        del self._streams[stream]

    async def _shutdown_active_tasks(self, tasks):
        if not tasks:
            return
        # NOTE: tasks finished with exception are reported
        # by Tast/Future __del__ method
        done, pending = await wait(tasks, timeout=self._shutdown_timeout)
        if not pending:
            return
        for task in pending:
            task.cancel()
        done, pending = await wait(pending, timeout=self._shutdown_timeout)
        for task in pending:
            self._loop.call_exception_handler({
                "message": f'{task} was not finished on stream server closing'
            })


class StreamServer(_BaseStreamServer):

    def __init__(self, client_connected_cb, host=None, port=None, *,
                 limit=_DEFAULT_LIMIT,
                 family=socket.AF_UNSPEC,
                 flags=socket.AI_PASSIVE, sock=None, backlog=100,
                 ssl=None, reuse_address=None, reuse_port=None,
                 ssl_handshake_timeout=None,
                 shutdown_timeout=60):
        # client_connected_cb name is consistent with legacy API
        # but it is long and ugly
        # any suggestion?

        super().__init__(client_connected_cb,
                         limit=limit,
                         shutdown_timeout=shutdown_timeout,
                         _asyncio_internal=True)
        self._host = host
        self._port = port
        self._family = family
        self._flags = flags
        self._sock = sock
        self._backlog = backlog
        self._ssl = ssl
        self._reuse_address = reuse_address
        self._reuse_port = reuse_port
        self._ssl_handshake_timeout = ssl_handshake_timeout

    async def _bind(self):
        def factory():
            protocol = _ServerStreamProtocol(self,
                                             self._limit,
                                             self._client_connected_cb,
                                             loop=self._loop,
                                             _asyncio_internal=True)
            return protocol
        return await self._loop.create_server(
            factory,
            self._host,
            self._port,
            start_serving=False,
            family=self._family,
            flags=self._flags,
            sock=self._sock,
            backlog=self._backlog,
            ssl=self._ssl,
            reuse_address=self._reuse_address,
            reuse_port=self._reuse_port,
            ssl_handshake_timeout=self._ssl_handshake_timeout)


if hasattr(socket, 'AF_UNIX'):
    # UNIX Domain Sockets are supported on this platform

    async def open_unix_connection(path=None, *,
                                   loop=None, limit=_DEFAULT_LIMIT, **kwds):
        """Similar to `open_connection` but works with UNIX Domain Sockets."""
        if loop is None:
            loop = events.get_event_loop()
        stream = Stream(mode=StreamMode.READWRITE,
                        limit=limit,
                        loop=loop,
                        _asyncio_internal=True)
        await loop.create_unix_connection(
            lambda: _StreamProtocol(stream,
                                    loop=loop,
                                    _asyncio_internal=True),
            path, **kwds)
        return stream, stream

    async def connect_unix(path=None, *,
                           limit=_DEFAULT_LIMIT,
                           ssl=None, sock=None,
                           server_hostname=None,
                           ssl_handshake_timeout=None):
        """Similar to `connect()` but works with UNIX Domain Sockets."""
        loop = events.get_running_loop()
        stream = Stream(mode=StreamMode.READWRITE,
                        limit=limit,
                        loop=loop,
                        _asyncio_internal=True)
        await loop.create_unix_connection(
            lambda: _StreamProtocol(stream,
                                    loop=loop,
                                    _asyncio_internal=True),
            path,
            ssl=ssl,
            sock=sock,
            server_hostname=server_hostname,
            ssl_handshake_timeout=ssl_handshake_timeout)
        return stream


    async def start_unix_server(client_connected_cb, path=None, *,
                                loop=None, limit=_DEFAULT_LIMIT, **kwds):
        """Similar to `start_server` but works with UNIX Domain Sockets."""
        if loop is None:
            loop = events.get_event_loop()

        def factory():
            protocol = _LegacyServerStreamProtocol(limit,
                                                   client_connected_cb,
                                                   loop=loop,
                                                   _asyncio_internal=True)
            return protocol

        return await loop.create_unix_server(factory, path, **kwds)

    class UnixStreamServer(_BaseStreamServer):

        def __init__(self, client_connected_cb, path=None, *,
                     limit=_DEFAULT_LIMIT,
                     sock=None,
                     backlog=100,
                     ssl=None,
                     ssl_handshake_timeout=None,
                     shutdown_timeout=60):
            super().__init__(client_connected_cb,
                             limit=limit,
                             shutdown_timeout=shutdown_timeout,
                             _asyncio_internal=True)
            self._path = path
            self._sock = sock
            self._backlog = backlog
            self._ssl = ssl
            self._ssl_handshake_timeout = ssl_handshake_timeout

        async def _bind(self):
            def factory():
                protocol = _ServerStreamProtocol(self,
                                                 self._limit,
                                                 self._client_connected_cb,
                                                 loop=self._loop,
                                                 _asyncio_internal=True)
                return protocol
            return await self._loop.create_unix_server(
                factory,
                self._path,
                start_serving=False,
                sock=self._sock,
                backlog=self._backlog,
                ssl=self._ssl,
                ssl_handshake_timeout=self._ssl_handshake_timeout)


class FlowControlMixin(protocols.Protocol):
    """Reusable flow control logic for StreamWriter.drain().

    This implements the protocol methods pause_writing(),
    resume_writing() and connection_lost().  If the subclass overrides
    these it must call the super methods.

    StreamWriter.drain() must wait for _drain_helper() coroutine.
    """

    def __init__(self, loop=None, *, _asyncio_internal=False):
        if loop is None:
            self._loop = events.get_event_loop()
        else:
            self._loop = loop
        if not _asyncio_internal:
            # NOTE:
            # Avoid inheritance from FlowControlMixin
            # Copy-paste the code to your project
            # if you need flow control helpers
            warnings.warn(f"{self.__class__} should be instaniated "
                          "by asyncio internals only, "
                          "please avoid its creation from user code",
                          DeprecationWarning)
        self._paused = False
        self._drain_waiter = None
        self._connection_lost = False

    def pause_writing(self):
        assert not self._paused
        self._paused = True
        if self._loop.get_debug():
            logger.debug("%r pauses writing", self)

    def resume_writing(self):
        assert self._paused
        self._paused = False
        if self._loop.get_debug():
            logger.debug("%r resumes writing", self)

        waiter = self._drain_waiter
        if waiter is not None:
            self._drain_waiter = None
            if not waiter.done():
                waiter.set_result(None)

    def connection_lost(self, exc):
        self._connection_lost = True
        # Wake up the writer if currently paused.
        if not self._paused:
            return
        waiter = self._drain_waiter
        if waiter is None:
            return
        self._drain_waiter = None
        if waiter.done():
            return
        if exc is None:
            waiter.set_result(None)
        else:
            waiter.set_exception(exc)

    async def _drain_helper(self):
        if self._connection_lost:
            raise ConnectionResetError('Connection lost')
        if not self._paused:
            return
        waiter = self._drain_waiter
        assert waiter is None or waiter.cancelled()
        waiter = self._loop.create_future()
        self._drain_waiter = waiter
        await waiter

    def _get_close_waiter(self, stream):
        raise NotImplementedError


class _BaseStreamProtocol(FlowControlMixin, protocols.Protocol):
    """Helper class to adapt between Protocol and StreamReader.

    (This is a helper class instead of making StreamReader itself a
    Protocol subclass, because the StreamReader has other potential
    uses, and to prevent the user of the StreamReader to accidentally
    call inappropriate methods of the protocol.)
    """

    _stream = None  # initialized in derived classes

    def __init__(self, loop=None,
                 *, _asyncio_internal=False):
        super().__init__(loop=loop, _asyncio_internal=_asyncio_internal)
        self._transport = None
        self._over_ssl = False
        self._closed = self._loop.create_future()

    def connection_made(self, transport):
        self._transport = transport
        self._over_ssl = transport.get_extra_info('sslcontext') is not None

    def connection_lost(self, exc):
        stream = self._stream
        if stream is not None:
            if exc is None:
                stream.feed_eof()
            else:
                stream.set_exception(exc)
        if not self._closed.done():
            if exc is None:
                self._closed.set_result(None)
            else:
                self._closed.set_exception(exc)
        super().connection_lost(exc)
        self._transport = None

    def data_received(self, data):
        stream = self._stream
        if stream is not None:
            stream.feed_data(data)

    def eof_received(self):
        stream = self._stream
        if stream is not None:
            stream.feed_eof()
        if self._over_ssl:
            # Prevent a warning in SSLProtocol.eof_received:
            # "returning true from eof_received()
            # has no effect when using ssl"
            return False
        return True

    def _get_close_waiter(self, stream):
        return self._closed

    def __del__(self):
        # Prevent reports about unhandled exceptions.
        # Better than self._closed._log_traceback = False hack
        closed = self._get_close_waiter(self._stream)
        if closed.done() and not closed.cancelled():
            closed.exception()


class _StreamProtocol(_BaseStreamProtocol):
    _source_traceback = None

    def __init__(self, stream, loop=None,
                 *, _asyncio_internal=False):
        super().__init__(loop=loop, _asyncio_internal=_asyncio_internal)
        self._source_traceback = stream._source_traceback
        self._stream_wr = weakref.ref(stream, self._on_gc)
        self._reject_connection = False

    def _on_gc(self, wr):
        transport = self._transport
        if transport is not None:
            # connection_made was called
            context = {
                'message': ('An open stream object is being garbage '
                            'collected; call "stream.close()" explicitly.')
            }
            if self._source_traceback:
                context['source_traceback'] = self._source_traceback
            self._loop.call_exception_handler(context)
            transport.abort()
        else:
            self._reject_connection = True
        self._stream_wr = None

    @property
    def _stream(self):
        if self._stream_wr is None:
            return None
        return self._stream_wr()

    def connection_made(self, transport):
        if self._reject_connection:
            context = {
                'message': ('An open stream was garbage collected prior to '
                            'establishing network connection; '
                            'call "stream.close()" explicitly.')
            }
            if self._source_traceback:
                context['source_traceback'] = self._source_traceback
            self._loop.call_exception_handler(context)
            transport.abort()
            return
        super().connection_made(transport)
        stream = self._stream
        if stream is None:
            return
        stream.set_transport(transport)
        stream._protocol = self

    def connection_lost(self, exc):
        super().connection_lost(exc)
        self._stream_wr = None


class _LegacyServerStreamProtocol(_BaseStreamProtocol):
    def __init__(self, limit, client_connected_cb, loop=None,
                 *, _asyncio_internal=False):
        super().__init__(loop=loop, _asyncio_internal=_asyncio_internal)
        self._client_connected_cb = client_connected_cb
        self._limit = limit

    def connection_made(self, transport):
        super().connection_made(transport)
        stream = Stream(mode=StreamMode.READWRITE,
                        transport=transport,
                        protocol=self,
                        limit=self._limit,
                        loop=self._loop,
                        _asyncio_internal=True)
        self._stream = stream
        res = self._client_connected_cb(self._stream, self._stream)
        if coroutines.iscoroutine(res):
            self._loop.create_task(res)

    def connection_lost(self, exc):
        super().connection_lost(exc)
        self._stream = None


class _ServerStreamProtocol(_BaseStreamProtocol):
    def __init__(self, server, limit, client_connected_cb, loop=None,
                 *, _asyncio_internal=False):
        super().__init__(loop=loop, _asyncio_internal=_asyncio_internal)
        assert self._closed
        self._client_connected_cb = client_connected_cb
        self._limit = limit
        self._server = server
        self._task = None

    def connection_made(self, transport):
        super().connection_made(transport)
        stream = Stream(mode=StreamMode.READWRITE,
                        transport=transport,
                        protocol=self,
                        limit=self._limit,
                        loop=self._loop,
                        _asyncio_internal=True)
        self._stream = stream
        # TODO: log a case when task cannot be created.
        # Usualy it means that _client_connected_cb
        # has incompatible signature.
        self._task = self._loop.create_task(
            self._client_connected_cb(self._stream))
        self._server._attach(stream, self._task)

    def connection_lost(self, exc):
        super().connection_lost(exc)
        self._server._detach(self._stream, self._task)
        self._stream = None


def _swallow_unhandled_exception(task):
    # Do a trick to suppress unhandled exception
    # if stream.write() was used without await and
    # stream.drain() was paused and resumed with an exception

    # TODO: add if not task.cancelled() check!!!!
    task.exception()


class Stream:
    """Wraps a Transport.

    This exposes write(), writelines(), [can_]write_eof(),
    get_extra_info() and close().  It adds drain() which returns an
    optional Future on which you can wait for flow control.  It also
    adds a transport property which references the Transport
    directly.
    """

    # TODO: add __aenter__ / __aexit__ to close stream

    _source_traceback = None

    def __init__(self, mode, *,
                 transport=None,
                 protocol=None,
                 loop=None,
                 limit=_DEFAULT_LIMIT,
                 _asyncio_internal=False):
        if not _asyncio_internal:
            warnings.warn(f"{self.__class__} should be instaniated "
                          "by asyncio internals only, "
                          "please avoid its creation from user code",
                          DeprecationWarning)
        self._mode = mode
        self._transport = transport
        self._protocol = protocol

        # The line length limit is  a security feature;
        # it also doubles as half the buffer limit.

        if limit <= 0:
            raise ValueError('Limit cannot be <= 0')

        self._limit = limit
        if loop is None:
            self._loop = events.get_event_loop()
        else:
            self._loop = loop
        self._buffer = bytearray()
        self._eof = False    # Whether we're done.
        self._waiter = None  # A future used by _wait_for_data()
        self._exception = None
        self._paused = False
        self._complete_fut = self._loop.create_future()
        self._complete_fut.set_result(None)

        if self._loop.get_debug():
            self._source_traceback = format_helpers.extract_stack(
                sys._getframe(1))

    def __repr__(self):
        info = [self.__class__.__name__]
        info.append(f'mode={self._mode}')
        if self._buffer:
            info.append(f'{len(self._buffer)} bytes')
        if self._eof:
            info.append('eof')
        if self._limit != _DEFAULT_LIMIT:
            info.append(f'limit={self._limit}')
        if self._waiter:
            info.append(f'waiter={self._waiter!r}')
        if self._exception:
            info.append(f'exception={self._exception!r}')
        if self._transport:
            info.append(f'transport={self._transport!r}')
        if self._paused:
            info.append('paused')
        return '<{}>'.format(' '.join(info))

    @property
    def mode(self):
        return self._mode

    @property
    def transport(self):
        return self._transport

    def write(self, data):
        self._mode._check_write()
        self._transport.write(data)
        return self._fast_drain()

    def writelines(self, data):
        self._mode._check_write()
        self._transport.writelines(data)
        return self._fast_drain()

    def _fast_drain(self):
        # The helper tries to use fast-path to return already existing complete future
        # object if underlying transport is not paused and actual waiting for writing
        # resume is not needed
        exc = self.exception()
        if exc is not None:
            fut = self._loop.create_future()
            fut.set_exception(exc)
            return fut
        if not self._transport.is_closing():
            if self._protocol._connection_lost:
                fut = self._loop.create_future()
                fut.set_exception(ConnectionResetError('Connection lost'))
                return fut
            if not self._protocol._paused:
                # fast path, the stream is not paused
                # no need to wait for resume signal
                return self._complete_fut
        ret = self._loop.create_task(self.drain())
        ret.add_done_callback(_swallow_unhandled_exception)
        return ret

    def write_eof(self):
        self._mode._check_write()
        return self._transport.write_eof()

    def can_write_eof(self):
        if not self._mode.is_write():
            return False
        return self._transport.can_write_eof()

    def close(self):
        self._transport.close()
        return self._protocol._get_close_waiter(self)

    def is_closing(self):
        return self._transport.is_closing()

    async def abort(self):
        self._transport.abort()
        await self.wait_closed()

    async def wait_closed(self):
        await self._protocol._get_close_waiter(self)

    def get_extra_info(self, name, default=None):
        return self._transport.get_extra_info(name, default)

    async def drain(self):
        """Flush the write buffer.

        The intended use is to write

          w.write(data)
          await w.drain()
        """
        self._mode._check_write()
        exc = self.exception()
        if exc is not None:
            raise exc
        if self._transport.is_closing():
            # Wait for protocol.connection_lost() call
            # Raise connection closing error if any,
            # ConnectionResetError otherwise
            await sleep(0)
        await self._protocol._drain_helper()

    async def sendfile(self, file, offset=0, count=None, *, fallback=True):
        await self.drain()  # check for stream mode and exceptions
        return await self._loop.sendfile(self._transport, file,
                                         offset, count, fallback=fallback)

    def exception(self):
        return self._exception

    def set_exception(self, exc):
        self._exception = exc

        waiter = self._waiter
        if waiter is not None:
            self._waiter = None
            if not waiter.cancelled():
                waiter.set_exception(exc)

    def _wakeup_waiter(self):
        """Wakeup read*() functions waiting for data or EOF."""
        waiter = self._waiter
        if waiter is not None:
            self._waiter = None
            if not waiter.cancelled():
                waiter.set_result(None)

    def set_transport(self, transport):
        if transport is self._transport:
            return
        assert self._transport is None, 'Transport already set'
        self._transport = transport

    def _maybe_resume_transport(self):
        if self._paused and len(self._buffer) <= self._limit:
            self._paused = False
            self._transport.resume_reading()

    def feed_eof(self):
        self._mode._check_read()
        self._eof = True
        self._wakeup_waiter()

    def at_eof(self):
        """Return True if the buffer is empty and 'feed_eof' was called."""
        self._mode._check_read()
        return self._eof and not self._buffer

    def feed_data(self, data):
        self._mode._check_read()
        assert not self._eof, 'feed_data after feed_eof'

        if not data:
            return

        self._buffer.extend(data)
        self._wakeup_waiter()

        if (self._transport is not None and
                not self._paused and
                len(self._buffer) > 2 * self._limit):
            try:
                self._transport.pause_reading()
            except NotImplementedError:
                # The transport can't be paused.
                # We'll just have to buffer all data.
                # Forget the transport so we don't keep trying.
                self._transport = None
            else:
                self._paused = True

    async def _wait_for_data(self, func_name):
        """Wait until feed_data() or feed_eof() is called.

        If stream was paused, automatically resume it.
        """
        # StreamReader uses a future to link the protocol feed_data() method
        # to a read coroutine. Running two read coroutines at the same time
        # would have an unexpected behaviour. It would not possible to know
        # which coroutine would get the next data.
        if self._waiter is not None:
            raise RuntimeError(
                f'{func_name}() called while another coroutine is '
                f'already waiting for incoming data')

        assert not self._eof, '_wait_for_data after EOF'

        # Waiting for data while paused will make deadlock, so prevent it.
        # This is essential for readexactly(n) for case when n > self._limit.
        if self._paused:
            self._paused = False
            self._transport.resume_reading()

        self._waiter = self._loop.create_future()
        try:
            await self._waiter
        finally:
            self._waiter = None

    async def readline(self):
        """Read chunk of data from the stream until newline (b'\n') is found.

        On success, return chunk that ends with newline. If only partial
        line can be read due to EOF, return incomplete line without
        terminating newline. When EOF was reached while no bytes read, empty
        bytes object is returned.

        If limit is reached, ValueError will be raised. In that case, if
        newline was found, complete line including newline will be removed
        from internal buffer. Else, internal buffer will be cleared. Limit is
        compared against part of the line without newline.

        If stream was paused, this function will automatically resume it if
        needed.
        """
        self._mode._check_read()
        sep = b'\n'
        seplen = len(sep)
        try:
            line = await self.readuntil(sep)
        except exceptions.IncompleteReadError as e:
            return e.partial
        except exceptions.LimitOverrunError as e:
            if self._buffer.startswith(sep, e.consumed):
                del self._buffer[:e.consumed + seplen]
            else:
                self._buffer.clear()
            self._maybe_resume_transport()
            raise ValueError(e.args[0])
        return line

    async def readuntil(self, separator=b'\n'):
        """Read data from the stream until ``separator`` is found.

        On success, the data and separator will be removed from the
        internal buffer (consumed). Returned data will include the
        separator at the end.

        Configured stream limit is used to check result. Limit sets the
        maximal length of data that can be returned, not counting the
        separator.

        If an EOF occurs and the complete separator is still not found,
        an IncompleteReadError exception will be raised, and the internal
        buffer will be reset.  The IncompleteReadError.partial attribute
        may contain the separator partially.

        If the data cannot be read because of over limit, a
        LimitOverrunError exception  will be raised, and the data
        will be left in the internal buffer, so it can be read again.
        """
        self._mode._check_read()
        seplen = len(separator)
        if seplen == 0:
            raise ValueError('Separator should be at least one-byte string')

        if self._exception is not None:
            raise self._exception

        # Consume whole buffer except last bytes, which length is
        # one less than seplen. Let's check corner cases with
        # separator='SEPARATOR':
        # * we have received almost complete separator (without last
        #   byte). i.e buffer='some textSEPARATO'. In this case we
        #   can safely consume len(separator) - 1 bytes.
        # * last byte of buffer is first byte of separator, i.e.
        #   buffer='abcdefghijklmnopqrS'. We may safely consume
        #   everything except that last byte, but this require to
        #   analyze bytes of buffer that match partial separator.
        #   This is slow and/or require FSM. For this case our
        #   implementation is not optimal, since require rescanning
        #   of data that is known to not belong to separator. In
        #   real world, separator will not be so long to notice
        #   performance problems. Even when reading MIME-encoded
        #   messages :)

        # `offset` is the number of bytes from the beginning of the buffer
        # where there is no occurrence of `separator`.
        offset = 0

        # Loop until we find `separator` in the buffer, exceed the buffer size,
        # or an EOF has happened.
        while True:
            buflen = len(self._buffer)

            # Check if we now have enough data in the buffer for `separator` to
            # fit.
            if buflen - offset >= seplen:
                isep = self._buffer.find(separator, offset)

                if isep != -1:
                    # `separator` is in the buffer. `isep` will be used later
                    # to retrieve the data.
                    break

                # see upper comment for explanation.
                offset = buflen + 1 - seplen
                if offset > self._limit:
                    raise exceptions.LimitOverrunError(
                        'Separator is not found, and chunk exceed the limit',
                        offset)

            # Complete message (with full separator) may be present in buffer
            # even when EOF flag is set. This may happen when the last chunk
            # adds data which makes separator be found. That's why we check for
            # EOF *ater* inspecting the buffer.
            if self._eof:
                chunk = bytes(self._buffer)
                self._buffer.clear()
                raise exceptions.IncompleteReadError(chunk, None)

            # _wait_for_data() will resume reading if stream was paused.
            await self._wait_for_data('readuntil')

        if isep > self._limit:
            raise exceptions.LimitOverrunError(
                'Separator is found, but chunk is longer than limit', isep)

        chunk = self._buffer[:isep + seplen]
        del self._buffer[:isep + seplen]
        self._maybe_resume_transport()
        return bytes(chunk)

    async def read(self, n=-1):
        """Read up to `n` bytes from the stream.

        If n is not provided, or set to -1, read until EOF and return all read
        bytes. If the EOF was received and the internal buffer is empty, return
        an empty bytes object.

        If n is zero, return empty bytes object immediately.

        If n is positive, this function try to read `n` bytes, and may return
        less or equal bytes than requested, but at least one byte. If EOF was
        received before any byte is read, this function returns empty byte
        object.

        Returned value is not limited with limit, configured at stream
        creation.

        If stream was paused, this function will automatically resume it if
        needed.
        """
        self._mode._check_read()

        if self._exception is not None:
            raise self._exception

        if n == 0:
            return b''

        if n < 0:
            # This used to just loop creating a new waiter hoping to
            # collect everything in self._buffer, but that would
            # deadlock if the subprocess sends more than self.limit
            # bytes.  So just call self.read(self._limit) until EOF.
            blocks = []
            while True:
                block = await self.read(self._limit)
                if not block:
                    break
                blocks.append(block)
            return b''.join(blocks)

        if not self._buffer and not self._eof:
            await self._wait_for_data('read')

        # This will work right even if buffer is less than n bytes
        data = bytes(self._buffer[:n])
        del self._buffer[:n]

        self._maybe_resume_transport()
        return data

    async def readexactly(self, n):
        """Read exactly `n` bytes.

        Raise an IncompleteReadError if EOF is reached before `n` bytes can be
        read. The IncompleteReadError.partial attribute of the exception will
        contain the partial read bytes.

        if n is zero, return empty bytes object.

        Returned value is not limited with limit, configured at stream
        creation.

        If stream was paused, this function will automatically resume it if
        needed.
        """
        self._mode._check_read()
        if n < 0:
            raise ValueError('readexactly size can not be less than zero')

        if self._exception is not None:
            raise self._exception

        if n == 0:
            return b''

        while len(self._buffer) < n:
            if self._eof:
                incomplete = bytes(self._buffer)
                self._buffer.clear()
                raise exceptions.IncompleteReadError(incomplete, n)

            await self._wait_for_data('readexactly')

        if len(self._buffer) == n:
            data = bytes(self._buffer)
            self._buffer.clear()
        else:
            data = bytes(self._buffer[:n])
            del self._buffer[:n]
        self._maybe_resume_transport()
        return data

    def __aiter__(self):
        self._mode._check_read()
        return self

    async def __anext__(self):
        val = await self.readline()
        if val == b'':
            raise StopAsyncIteration
        return val

    # async def __aenter__(self):
    #     return self

    # async def __aexit__(self, exc_type, exc_val, exc_tb):
    #     await self.close()
