package com.alexbatalov.fallout2ce;

import android.os.ParcelFileDescriptor;

import java.io.Closeable;
import java.io.IOException;

// An archive the player chose, read by the app's import library
// (src/import_archive.cc through import_archive_jni.cc): .zip (Deflate64 of
// Windows' big archives too), .7z, .rar (4 and 5), .tar(.gz/.xz).
final class NativeArchive implements Closeable {
    static {
        System.loadLibrary("fallout2-import");
    }

    // Why it failed (ImportArchiveError).
    static final int UNREADABLE = 1;
    static final int NOT_SEEKABLE = 2;
    static final int ENCRYPTED = 3;
    static final int MULTI_VOLUME = 4;
    static final int UNSUPPORTED = 5;
    static final int CORRUPT = 6;

    static final class Failure extends IOException {
        final int reason;

        Failure(int reason, String message) {
            super(message);
            this.reason = reason;
        }
    }

    // While the archive is opened (some kinds are read through for their
    // names): an exception stops it (cancel) and goes on to the caller.
    interface Progress {
        void update(long done, long total) throws IOException;
    }

    // Gets the entries' data; an exception stops the reading and goes on to
    // the caller.
    interface Sink {
        void begin(int index) throws IOException;

        void data(byte[] buffer, int length) throws IOException;

        void end(int index) throws IOException;
    }

    // The archive's files (not folders or links): path from its root with
    // "/", size (0 when not told), flags (1 - encrypted, 2 - compressed in a
    // way that can't be read).
    final String[] paths;
    final long[] sizes;
    final int[] flags;

    private final byte[] buffer = new byte[256 * 1024];
    private long handle;

    private NativeArchive(long handle) {
        this.handle = handle;
        paths = nativePaths(handle);
        sizes = nativeSizes(handle);
        flags = nativeFlags(handle);
    }

    // [descriptor] stays the caller's.
    static NativeArchive open(ParcelFileDescriptor descriptor, Progress progress) throws IOException {
        return new NativeArchive(nativeOpen(descriptor.getFd(), progress));
    }

    // Reads [indices] into [sink] in the archive's own order.
    void read(int[] indices, Sink sink) throws IOException {
        if (handle == 0) {
            throw new IOException("closed");
        }
        nativeRead(handle, indices, buffer, sink);
    }

    @Override
    public void close() {
        if (handle != 0) {
            nativeClose(handle);
            handle = 0;
        }
    }

    private static native long nativeOpen(int fd, Progress progress) throws IOException;

    private static native String[] nativePaths(long handle);

    private static native long[] nativeSizes(long handle);

    private static native int[] nativeFlags(long handle);

    private static native void nativeRead(long handle, int[] indices, byte[] buffer, Sink sink) throws IOException;

    private static native void nativeClose(long handle);
}
