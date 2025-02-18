Most of libav.js's API is libav's API, and for such functions, you can consult
FFmpeg's documentation. Not every function is exposed, of course; see funcs.json
for a list of exposed functions or to add new functions.

Functions that use double-pointers are exposed as `_js` metafunctions that
return single pointers.

The following additional functionality is provided by libav.js itself, divided
here by the libav component it belongs to. Please read `libav.types.in.d.ts` for
type declarations.


# AVFormat 

## Muxing

### `ff_init_muxer`
```
ff_init_muxer(
    opts: {
        oformat?: number,
        format_name?: string,
        filename?: string,
        device?: boolean,
        open?: boolean,
        codecpars?: boolean
    },
    streamCtxs: [number, number, number][]
): Promise<[number, number, number, number[]]>
```

Initializes a muxer all at once, opening the file and initializing the format
context. You can provide the format as a libav numerical code (`oformat`) or as
a name (`format_name`), the filename to write to (which can of course be a
device) (`filename`), and/or create it as a writer device automatically
(`device`). You have the option not to open the file (`open=false`), in which
case you will need to provide your own `pb`.

For the streams to mux, each stream is in the form `[number, number, number]`,
consisting of the codec context, `time_base_num`, and `time_base_den`,
respectively. If `opts.codecpars` is set, use a codec parameters (codecpar), not
a codec context.

Returns `[output context (oc), format, writer context (pb), stream contexts]`.
Usually called as `[oc, fmt, pb] = await ff_init_muxer(...)`.

To write, you must first use `libav.avformat_write_header`, and after writing
all packets, you must use `libav.av_write_trailer`. You may write packets with
`libav.ff_write_multi` (below), or directly using libav APIs.


### `ff_write_multi`
```
ff_write_multi(
    oc: number, pkt: number, inPackets: (Packet | number)[], interleave?: boolean
): Promise<void>
```

Write packets to an output context. You need to not just provide the packet(s)
(`inPackets`), but allocate space for packets in libav format, so there's
somewhere to write them to temporarily (`pkt`). Packets may be in libav.js
`Packet` format, or `AVPacket` pointers. Use `av_packet_alloc` (and eventually,
`av_packet_free`) for that, or get it from one of the AVCodec metafunctions.
`interleave` means that it will use `av_interleaved_write_frame`, and if
`interleave===false`, it will use `av_write_frame` instead. `interleave`
defaults to true, and this is usually the right option, but if your input is
already interleaved, you should set this to false.


### `ff_free_muxer`
```
ff_free_muxer(oc: number, pb: number): Promise<void>
```

Frees the context generated by `ff_init_muxer`.


## Demuxing

### `ff_init_demuxer_file`
```
ff_init_demuxer_file(
    filename: string, fmt?: string
): Promise<[number, Stream[]]>
```

Initializes a demuxer from the given filename, which can be a reader device.
Optionally takes the expected format as a string.

Returns `[format context (fmt_ctx), streams]`. Streams are of the `Stream` type.

Note that there's no equivalent of `ff_free_muxer`, because
`ff_init_demuxer_file` only initializes one libav object. Free `fmt_ctx` with
`avformat_close_input_js`.


### `ff_read_multi`
```
ff_read_multi(
    fmt_ctx: number, pkt: number, devfile?: string, opts?: {
        limit?: number, // OUTPUT limit, in bytes
        devLimit?: number, // INPUT limit, in bytes (don't read if less than this much data is available)
        unify?: boolean, // If true, unify the packets into a single stream (called 0), so that the output is in the same order as the input
        copyoutPacket?: string // Version of ff_copyout_packet to use
    }
): Promise<[number, Record<number, Packet[]>]>
```

Read some packets from a format context. Like `ff_write_multi`, you must provide
`pkt`.

By default, this will read as much as it can, which is typically the entire
file. If using a device in this default mode, you will need to feed it data, and
won't get anything back until it's done.

To limit how much data is sent, either use `limit`, or use `devfile` and
`devLimit` (`devfile` is not part of `opts` for historical reasons). `limit`
limits how much data `ff_read_multi` outputs to one packet more than the number
of bytes you request. If you set `devfile` and `devLimit`, then it only reads
until the device file is down to `devLimit` bytes. Note that to use `opts`
without `devfile`, you must set `devfile` to `null`; you cannot skip it.

Returns `[result, packets]`. The result is the result code from the underlying
read; `0` is success, but you can also expect `-libav.EAGAIN` (if you hit a
limit) or `libav.AVERROR_EOF` (at the end of the file).

The returned packets are in a record indexed by the stream index. Those indices
are the indices of the `Stream` objects in the `Stream[]` array given by
`ff_init_demuxer_file`. Alternatively, you can use `unify`, in which case all
packets will be in input order in a single array, `packets[0]`.

There are multiple versions of `ff_copyout_packet`, only one of which is
actually used to copy out packets. See the documentation of `ff_copyout_packet`
below for how to use the `copyoutPacket` option.


## Data manipulation

### `ff_copyout_packet` and variants
```
ff_copyout_packet(pkt: number): Promise<Packet>
```

Variants: `ff_copyout_packet_ptr`

Copy a packet from internal libav memory (`pkt`) as a libav.js object.

The `ff_copyout_packet_ptr` function is also available, and copies the packet
into a separate `AVPacket` pointer, instead of actually copying out any data.
This is a good compromise if you're building pipelines, e.g. reading then
decoding, to avoid copying data back and forth when that data is just going back
into libav.js. Be careful, though! `ff_read_multi` reads from every stream, and
if you're only using data from one of them, copied packets using
`ff_copyout_packet_ptr` will leak memory! Use `ff_copyout_packet_ptr` carefully.

Metafunctions that use `ff_copyout_packet` internally, namely `ff_read_multi`,
have a configuration option, `copyoutPacket`, to specify which version of
`ff_copyout_packet` to use. It is a string option, accepting the following
values: `"default", "ptr"`.


### `ff_copyin_packet`
```
ff_copyin_packet(pktPtr: number, packet: Packet | number): Promise<void>
```

Copy a packet as a libav.js object (`packet`) into libav memory (`pktPtr`). Also
works to duplicate a packet that is already an `AVPacket` pointer as a number.


# AVCodec

## Encoding

### `ff_init_encoder`
```
ff_init_encoder(
    name: string, opts?: {
        ctx?: AVCodecContextProps, options?: Record<string, string>
    }
): Promise<[number, number, number, number, number]>
```

Allocate and initialize an encoder (whether audio or video). Sometimes, all that
is sufficient is the name, in libav style, e.g. `libopus` or `aac`. Common
features like bitrate are in `ctx` (see the description of `AVCodecContextProps`
in libav.types.in.d.ts). `options` is for codec-specific options.

Returns a *lot* of things, some of which aren't always needed: `[codec, codec
context (c), frame, packet (pkt), frame size]`. Usually called as `[, c, frame,
pkt, frame_size] = await ff_init_encoder(...)`.


### `ff_encode_multi`
```
ff_encode_multi(
    ctx: number, frame: number, pkt: number, inFrames: (Frame | number)[],
    fin?: boolean
): Promise<Packet[]>
```

Encode multiple frames into packets. Set `fin` if these are the last frames;
otherwise the arguments should be obvious. Note that it's fine to set `inFrames`
to `[]` to encode no frames, typically to set `fin`. The frames may be `AVFrame`
pointers, as numbers.


### `ff_free_encoder`
```
ff_free_encoder(
    c: number, frame: number, pkt: number
): Promise<void>
```

Free the things allocated by `ff_init_encoder`.


## Decoding

### `ff_init_decoder`
```
ff_init_decoder(
    name: string | number, codecpar?: number
): Promise<[number, number, number, number]>
```

Initialize a decoder. `name` can be a string name (e.g. `"libopus"`) *or* an
internal identifier. Usually, an internal identifier would come from a `Stream`
from `ff_init_demuxer_file`, in which case `codecpar` would also come from that
`Stream`.

Returns a *lot* of things, some of which aren't alwayys needed: `[codec, codec
context (c), packet (pkt), frame]`. Usually called as `[, c, pkt, frame] =
ff_init_decoder(...)`.


### `ff_decode_multi`
```
ff_decode_multi(
    ctx: number, pkt: number, frame: number, inPackets: (Packet | number)[],
    config?: boolean | {
        fin?: boolean,
        ignoreErrors?: boolean,
        copyoutFrame?: string
    }
): Promise<Frame[]>
```

Decode multiple packets, which may be libav.js `Packet`s or `AVPacket` pointers.
`config` can be set to `true` as `fin`, which means these are the last packets.
Alternatively, `config` can be set to an object, and `ignoreErrors` will attempt
to continue decoding in the case of errors.

There are multiple versions of `ff_copyout_frame`, only one of which is actually
used to copy out frames. See the documentation of `ff_copyout_frame` below for
how to use the `copyoutFrame` option.


### `ff_decode_filter_multi`

Combination of `ff_decode_multi` and `ff_filter_multi`. Documented with
`ff_filter_multi`, below.


### `ff_free_decoder`
```
ff_free_decoder(
    c: number, pkt: number, frame: number
): Promise<void>
```

Free the things allocated by `ff_init_decoder`.


## Data manipulation

### `ff_copyout_frame` and variants
```
ff_copyout_frame(frame: number): Promise<Frame>
```

Variants: `ff_copyout_frame_video`, `ff_copyout_frame_video_packed`,
`ff_copyout_frame_video_imagedata`, `ff_copyout_frame_ptr`

Copy a frame out of internal libav memory (`frame`) as a libav.js object.
`ff_copyout_frame` supports video frames, but if you know a frame is a video
frame, you can bypass the check by using `ff_copyout_frame_video` instead.

The format of video frames as `Frame`s is complicated by how they're stored
internally in libav. For a simpler, packed format, in which all pixels are in a
single `Uint8Array`, use `ff_copyout_frame_video_packed`. Or, to copy out video
frames directly as `ImageData` objects instead of libav.js `Frame`s at all, use
`ff_copyout_frame_video_imagedata`. `ImageData` is only available in browsers.
Further, `ff_copyout_frame_video_imagedata` does *not* convert the image format,
so video frames must already be in RGBA (*not* RGB32!) format to use it.

The `ff_copyout_frame_ptr` function is also available, and copies the frame into
a separate `AVFrame` pointer, instead of actually copying out any data. This is
a good compromise if you're building pipelines, e.g. decoding and then
filtering, to avoid copying data back and forth when that data is just going
back into libav.js.

Metafunctions that use `ff_copyout_frame` internally, namely `ff_decode_multi`
and `ff_filter_multi`, have a configuration option, `copyoutFrame`, to specify
which version of `ff_copyout_frame` to use. It is a string option, accepting the
following values: `"default", "video", "video_packed", "ImageData", "ptr"`.


### `ff_copyin_frame`
```
ff_copyin_frame(framePtr: number, frame: Frame | number): Promise<void>
```

Copy a libav.js Frame object (`frame`) into libav memory (`framePtr`). Also
works if `frame` is another `AVFrame` pointer, e.g. as created by
`ff_copyout_frame_ptr`.


# AVFilter

### `ff_init_filter_graph`
```
ff_init_filter_graph(
    filters_descr: string,
    input: FilterIOSettings | FilterIOSettings[],
    output: FilterIOSettings | FilterIOSettings[]
): Promise<[number, number | number[], number | number[]]>;
```

Create a filter graph. Most of the context is `filters_descr` is either a simple
description (as would be passed to `-vf` or `-af` in ffmpeg) or a complex
description (as would be passed to `-filter_complex`).

`FilterIOSettings` describes things like the sample rate/frame rate and sample
format/pixel format. A filter graph can have multiple inputs and multiple
outputs, in which case `input` and `output` can be arrays.

If there is one input, the input pad is named `in`. Otherwise, the input pads
are named `in0`, `in1`, etc. If there is one output, the output pad is named
`out`. Otherwise, the output pads are named `out0`, `out1`, etc.

Returns `[filter graph, source context(s), sink context(s)]`. If there are
multiple inputs (sources), then source contexts is an array, and if there are
multiple outputs (sinks), then sink contexts is an array.


### `ff_filter_multi`
```
ff_filter_multi@sync(
    srcs: number | number[], buffersink_ctx: number, framePtr: number,
    inFrames: (Frame | number)[] | (Frame | number)[][], config?: boolean | {
        fin?: boolean,
        copyoutFrame?: string
    }
): @promise@Frame[]@;

```

Filter one or more frames. Takes one or more source contexts (`srcs`), but *only
one sink context* (`buffersink_ctx`). If you need to filter with multiple
outputs, you need to use the lower-level libav functions. `inFrames` should be
an array of frames if there's only one source, or an array of arrays if there
are multiple sources. Frames may be libav.js `Frame`s or `AVFrame` pointers as
numbers.

Also requires a frame pointer (`framePtr`) to use for temporary storage, which
can be allocated directly or by an encoding/decoding metafunction.

`config` can be used to pass configuration options, or pass `true` as `config`
as an equivalent of `config.fin`. Set `fin` if these are the last input frames.

There are multiple versions of `ff_copyout_frame`, only one of which is actually
used to copy out frames. See the documentation of `ff_copyout_frame` above for
how to use the `copyoutFrame` option.


### `ff_decode_filter_multi`
```
ff_decode_filter_multi(
    ctx: number, buffersrc_ctx: number, buffersink_ctx: number, pkt: number,
    frame: number, inPackets: (Packet | number)[],
    config?: boolean | {
        fin?: boolean,
        ignoreErrors?: boolean,
        copyoutFrame?: string
    }
): Promise<Frame[]>
```

Combination of `ff_decode_multi` and `ff_filter_multi` in a single function.
Useful to avoid the overhead of copying data and waiting for Promises between
decoding and filtering.

`await ff_decode_filter_multi(ctx, src, sink, pkt, frame, packets, config)` is
equivalent to `await ff_filter_multi(src, sink, frame, await
ff_decode_multi(ctx, pkt, frame, packets), config)`. That is, this really just
combines the two calls. However, internally, neither frame copying nor Promises
are used.


# Filesystem

The `readFile`, `writeFile`, `unlink`, and `mkdev` functions are provided
directly from Emscripten's filesystem module. In addition, functions to create
streaming devices are provided. These are further documented in [IO.md](IO.md).

## Reader device

### `mkreaderdev`
```
mkreaderdev(name: string, mode?: number): Promise<void>
```

Make a reader device. This is used to stream data, and acts like a Unix
character device. The mode is usually unnecessary.


### `mkblockreaderdev`
```
mkblockreaderdev(name: string, size: number): Promise<void>
```

Similar to `mkreaderdev`, but `mkreaderdev` creates a character device
(streaming device), whereas `mkblockreaderdev` creates a block device, so it
will have a size and random access.

To intercept read requests from the block reader device(s), you must set
`libav.onblockread` to a function `(name: string, position: number, length:
number) => void`.

Because `onblockread` is a callback, it is usually possible to create a block
reader device and its callback and then use the API as if no devices are used.


### `ff_reader_dev_send`
```
ff_reader_dev_send(name: string, data: Uint8Array): Promise<void>
```

Send data to a reader device. There is no limit imposed by libav.js to how much
data a reader device can buffer.


### `ff_block_reader_dev_send`
```
ff_block_reader_dev_send(
    name: string, position: number, data: Uint8Array
): Promise<void>
```

Similar to `ff_reader_dev_send`, but for block reader devices, and has a
position specified for the data.


### `ff_reader_dev_waiting`
```
ff_reader_dev_waiting(): Promise<boolean>
```

Returns `true` if one or more reader or block reader devices are waiting for
data. Typically, this is used along with `ff_init_demuxer_file` and
`ff_read_multi` to feed in data. If instead of awaiting those promises, you
store the promises aside, you can loop with `while (await
libav.ff_reader_dev_waiting())` and send data, *then* await the file-reading
promise. In this way, you can control the transfer of input data without having
to send entire files or predict how much data might be needed.


## Writer device

### `mkwriterdev`
```
mkwriterdev(name: string, mode?: number): Promise<void>
```

Make a writer device. This is generally used to stream data, but some formats
aren't streamable, so this acts a bit more like a Unix *block* device.

To receive data from the writer device(s), you must set `libav.onwrite` to a
function `(name: string, position: number, buffer: Uint8Array) => void`.


### `mkstreamwriterdev`
```
mkstreamwriterdev(name: string, mode? number): Promise<void>
```

Make a stream writer device. Identical to a writer device except that seeking
is not allowed, so libav treats it like a stream. Most formats either don't
care or simply won't work with a stream, but certain formats (like `wav` and
`matroska`) will behave differently if the output is a stream than if it's a
block device.

Receive data in the same way as with `mkwriterdev`.


# CLI

If a variant is used that employs the `cli` fragment, then the entire `ffmpeg`
and `ffprobe` CLIs are exposed, as well as the various libav interfaces.

### `ffmpeg`
```
ffmpeg(...args: (string | string[])[]): Promise<number>
```

Runs the `ffmpeg` CLI tool. `args` can be each string argument to `ffmpeg`, or
an array of strings, or any combination thereof. Returns `ffmpeg`'s exit code.

NOTE: ffmpeg 6.0 and later require threads for the ffmpeg CLI. libav.js *does*
support the ffmpeg CLI on unthreaded environments, but to do so, it uses an
earlier version of the CLI, from 5.1.3. The libraries are still modern, and if
running libav.js in threaded mode, the ffmpeg CLI is modern as well. As time
passes, these two versions will drift apart, so make sure you know whether
you're running in threaded mode or not!


### `ffprobe`
```
ffprobe(...args: (string | string[])[]): Promise<number>
```

Like `ffmpeg`, but for the `ffprobe` tool.
