package com.alexbatalov.fallout2ce;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;

import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

// The game's files from the player's own copy of Fallout 2, a folder or an
// archive of it (.zip, .7z, .rar, .tar: NativeArchive) put on the phone,
// copied where the game reads them (the app's files folder). The game is the
// shallowest folder with master.dat and critter.dat, at any depth. Mods (RPU...)
// and saves in it come along.
final class GameImport {
    // An earlier version's mark of a game copied in part (copies go through
    // GameFiles' staging folder now): such a game isn't started.
    private static final String INCOMPLETE_MARK = ".import-incomplete";

    private GameImport() {
    }

    // With a replacement not finished (GameFiles' journal) it isn't: the
    // caller finishes it first (GameFiles.finish).
    static boolean isInstalled(File target) {
        return target != null
            && new File(target, "master.dat").isFile()
            && new File(target, "critter.dat").isFile()
            && !new File(target, INCOMPLETE_MARK).exists()
            && !GameFiles.isCommitPending(target);
    }

    // A file of the chosen folder or archive, [path] with "/" from its root.
    static final class Entry {
        final String path;
        final long size;
        final Object handle;
        // Encrypted, or compressed in a way that can't be read (archives).
        final boolean encrypted;
        final boolean unsupported;

        Entry(String path, long size, Object handle, boolean encrypted, boolean unsupported) {
            this.path = path;
            this.size = size;
            this.handle = handle;
            this.encrypted = encrypted;
            this.unsupported = unsupported;
        }
    }

    // Gets the data of the entries read, one entry after another.
    interface EntrySink {
        void begin(Entry entry) throws IOException;

        void data(byte[] buffer, int length) throws IOException;

        void end(Entry entry) throws IOException;
    }

    abstract static class Source implements Closeable {
        abstract List<Entry> list() throws IOException;

        // Gives [wanted] (from `list`) to [sink], in the source's own order.
        abstract void read(List<Entry> wanted, EntrySink sink) throws IOException;

        @Override
        public void close() throws IOException {
        }
    }

    // A folder chosen through the system's picker (`ACTION_OPEN_DOCUMENT_TREE`).
    static final class FolderSource extends Source {
        private final ContentResolver resolver;
        private final Uri tree;

        FolderSource(ContentResolver resolver, Uri tree) {
            this.resolver = resolver;
            this.tree = tree;
        }

        @Override
        List<Entry> list() throws IOException {
            List<Entry> entries = new ArrayList<>();
            listChildren(DocumentsContract.getTreeDocumentId(tree), "", entries);
            return entries;
        }

        // One query per folder (the DocumentFile helpers ask per property).
        private void listChildren(String documentId, String prefix, List<Entry> entries) throws IOException {
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
            String[] projection = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE,
            };

            List<String[]> folders = new ArrayList<>();
            try (Cursor cursor = resolver.query(children, projection, null, null, null)) {
                if (cursor == null) {
                    throw new IOException("can't list " + prefix);
                }
                while (cursor.moveToNext()) {
                    String id = cursor.getString(0);
                    String name = cursor.getString(1);
                    String mime = cursor.getString(2);
                    long size = cursor.isNull(3) ? 0 : cursor.getLong(3);
                    if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                        folders.add(new String[] { id, prefix + name + "/" });
                    } else {
                        entries.add(new Entry(prefix + name, size, id, false, false));
                    }
                }
            }

            for (String[] folder : folders) {
                listChildren(folder[0], folder[1], entries);
            }
        }

        @Override
        void read(List<Entry> wanted, EntrySink sink) throws IOException {
            byte[] buffer = new byte[256 * 1024];
            for (Entry entry : wanted) {
                Uri uri = DocumentsContract.buildDocumentUriUsingTree(tree, (String) entry.handle);
                try (InputStream stream = resolver.openInputStream(uri)) {
                    if (stream == null) {
                        throw new IOException("can't read " + entry.path);
                    }
                    sink.begin(entry);
                    int read;
                    while ((read = stream.read(buffer)) > 0) {
                        sink.data(buffer, read);
                    }
                    sink.end(entry);
                }
            }
        }
    }

    // An archive chosen through the system's picker (`ACTION_OPEN_DOCUMENT`),
    // read in place through its descriptor. Opening it lists it (some kinds
    // are read through for that: [progress], which may cancel).
    static final class ArchiveSource extends Source {
        private final ParcelFileDescriptor descriptor;
        private final NativeArchive archive;
        private final List<Entry> entries = new ArrayList<>();

        ArchiveSource(ContentResolver resolver, Uri uri, NativeArchive.Progress progress) throws IOException {
            ParcelFileDescriptor opened;
            try {
                opened = resolver.openFileDescriptor(uri, "r");
            } catch (SecurityException e) {
                throw new IOException(e.getMessage(), e);
            }
            if (opened == null) {
                throw new IOException("can't open the archive");
            }
            descriptor = opened;

            try {
                archive = NativeArchive.open(descriptor, progress);
            } catch (IOException | RuntimeException e) {
                descriptor.close();
                throw e;
            }

            for (int index = 0; index < archive.paths.length; index++) {
                int flags = archive.flags[index];
                entries.add(new Entry(archive.paths[index], archive.sizes[index], index, (flags & 1) != 0, (flags & 2) != 0));
            }
        }

        @Override
        List<Entry> list() {
            return entries;
        }

        @Override
        void read(List<Entry> wanted, EntrySink sink) throws IOException {
            int[] indices = new int[wanted.size()];
            for (int position = 0; position < indices.length; position++) {
                indices[position] = (Integer) wanted.get(position).handle;
            }
            archive.read(indices, new NativeArchive.Sink() {
                @Override
                public void begin(int index) throws IOException {
                    sink.begin(entries.get(index));
                }

                @Override
                public void data(byte[] buffer, int length) throws IOException {
                    sink.data(buffer, length);
                }

                @Override
                public void end(int index) throws IOException {
                    sink.end(entries.get(index));
                }
            });
        }

        @Override
        public void close() throws IOException {
            archive.close();
            descriptor.close();
        }
    }

    // What was found: the game's folder in the source, what is copied.
    static final class Found {
        // "" or ending with "/".
        String root;
        final List<Entry> files = new ArrayList<>();
        long size;
        String language;
        final List<String> mods = new ArrayList<>();
        int saves;
        // Without Fallout 2's own files (master.dat, critter.dat): its mods
        // and configs only, for "Replace game" (the original files stay).
        boolean modsOnly;
    }

    // More than one game at the same depth: which one is meant isn't known.
    static final class SeveralGamesException extends IOException {
        final List<String> roots;

        SeveralGamesException(List<String> roots) {
            super("several games: " + roots);
            this.roots = roots;
        }
    }

    // Null without the game. Its files encrypted or compressed in a way that
    // can't be read: NativeArchive.Failure. [modsOnly]: a game without
    // Fallout 2's own files is found too (for "Replace game").
    static Found find(Source source, boolean modsOnly) throws IOException {
        List<Entry> entries = source.list();

        Set<String> paths = new HashSet<>();
        for (Entry entry : entries) {
            paths.add(entry.path.toLowerCase(Locale.ROOT));
        }

        // The game's folder: the shallowest with master.dat and critter.dat.
        List<String> roots = new ArrayList<>();
        for (Entry entry : entries) {
            String name = baseName(entry.path);
            String folder = entry.path.substring(0, entry.path.length() - name.length());
            if (name.equalsIgnoreCase("master.dat") && paths.contains((folder + "critter.dat").toLowerCase(Locale.ROOT))) {
                roots.add(folder);
            }
        }
        boolean withoutOriginals = false;
        if (roots.isEmpty() && modsOnly) {
            // Mods only: the shallowest folder with the game's config,
            // sfall's or the mods' folder.
            for (Entry entry : entries) {
                String name = baseName(entry.path);
                String lower = entry.path.toLowerCase(Locale.ROOT);
                int mods = ("/" + lower).indexOf("/mods/");
                if (name.equalsIgnoreCase("fallout2.cfg") || name.equalsIgnoreCase("ddraw.ini") || name.equalsIgnoreCase("sfall.dat")) {
                    roots.add(entry.path.substring(0, entry.path.length() - name.length()));
                } else if (mods != -1) {
                    roots.add(entry.path.substring(0, mods));
                }
            }
            withoutOriginals = true;
        }
        roots = shallowest(roots);
        if (roots.isEmpty()) {
            return null;
        }
        if (roots.size() > 1) {
            throw new SeveralGamesException(roots);
        }

        Found found = new Found();
        found.root = roots.get(0);
        found.modsOnly = withoutOriginals;
        for (Entry entry : entries) {
            if (!entry.path.startsWith(found.root)) {
                continue;
            }
            String path = entry.path.substring(found.root.length());
            if (isSkipped(path)) {
                continue;
            }
            if (entry.encrypted) {
                throw new NativeArchive.Failure(NativeArchive.ENCRYPTED, path);
            }
            if (entry.unsupported) {
                throw new NativeArchive.Failure(NativeArchive.UNSUPPORTED, path);
            }
            found.files.add(entry);
            found.size += entry.size;

            String lower = path.toLowerCase(Locale.ROOT);
            if (lower.startsWith("mods/") && lower.endsWith(".dat") && lower.indexOf('/', 5) == -1) {
                found.mods.add(path.substring(5, path.length() - 4));
            } else if (lower.startsWith("data/savegame/slot") && lower.endsWith("/save.dat")) {
                found.saves++;
            }
        }

        found.language = readLanguage(source, found);
        return found;
    }

    // The least deep of [folders], each once.
    private static List<String> shallowest(List<String> folders) {
        List<String> result = new ArrayList<>();
        int least = Integer.MAX_VALUE;
        for (String folder : folders) {
            int depth = depth(folder);
            if (depth < least) {
                least = depth;
                result.clear();
            }
            if (depth == least && !result.contains(folder)) {
                result.add(folder);
            }
        }
        return result;
    }

    private static int depth(String folder) {
        int depth = 0;
        for (int index = 0; index < folder.length(); index++) {
            if (folder.charAt(index) == '/') {
                depth++;
            }
        }
        return depth;
    }

    // Files of the computer's game nobody needs on the phone: Windows
    // programs and libraries, shortcuts, folder icons, macOS leftovers and
    // programs (CE's .app); and the engine's ce.dat (the app brings its own,
    // see MainActivity).
    private static boolean isSkipped(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        String name = baseName(lower);
        return lower.equals("ce.dat")
            || lower.startsWith("ce.dat/")
            || lower.startsWith("__macosx/")
            || ("/" + lower).contains(".app/")
            || name.startsWith("._")
            || name.equals(".ds_store")
            || name.equals("thumbs.db")
            || name.equals("desktop.ini")
            || name.endsWith(".exe")
            || name.endsWith(".dll")
            || name.endsWith(".lnk");
    }

    private static String baseName(String path) {
        int slash = path.lastIndexOf('/');
        return slash == -1 ? path : path.substring(slash + 1);
    }

    // `[system] language` of the copied fallout2.cfg ("english" without it).
    private static String readLanguage(Source source, Found found) throws IOException {
        List<Entry> config = new ArrayList<>();
        for (Entry entry : found.files) {
            if (entry.path.substring(found.root.length()).equalsIgnoreCase("fallout2.cfg")) {
                config.add(entry);
            }
        }
        if (config.isEmpty()) {
            return "english";
        }

        ByteArrayOutputStream text = new ByteArrayOutputStream();
        source.read(config, new EntrySink() {
            @Override
            public void begin(Entry entry) {
            }

            @Override
            public void data(byte[] buffer, int length) {
                // A config, not a megabyte.
                if (text.size() < 1024 * 1024) {
                    text.write(buffer, 0, length);
                }
            }

            @Override
            public void end(Entry entry) {
            }
        });

        String language = "english";
        for (String line : new String(text.toByteArray(), StandardCharsets.ISO_8859_1).split("\r?\n")) {
            String trimmed = line.trim();
            if (trimmed.toLowerCase(Locale.ROOT).startsWith("language=")) {
                language = trimmed.substring("language=".length()).trim();
            }
        }
        return language;
    }

    interface Progress {
        void update(long copied, long total, String path);

        boolean isCancelled();
    }

    static final class CancelledException extends IOException {
        CancelledException() {
            super("cancelled");
        }
    }

    // What putting [found] into the game's folder [target] does
    // (GameFiles.plan). [withSaves]: the source's saves come along (the
    // first install); else the installed ones are left as they are.
    static GameFiles.Plan plan(Found found, File target, boolean withSaves) {
        List<String> paths = new ArrayList<>();
        List<Long> sizes = new ArrayList<>();
        for (Entry entry : wanted(found, withSaves)) {
            paths.add(entry.path.substring(found.root.length()));
            sizes.add(entry.size);
        }
        return GameFiles.plan(paths, sizes, target, GameFiles.installedGameFiles(target), found.modsOnly);
    }

    private static List<Entry> wanted(Found found, boolean withSaves) {
        List<Entry> wanted = new ArrayList<>();
        for (Entry entry : found.files) {
            String path = entry.path.substring(found.root.length()).toLowerCase(Locale.ROOT);
            if (withSaves || !path.startsWith("data/savegame/")) {
                wanted.add(entry);
            }
        }
        return wanted;
    }

    // Puts [found] into the game's folder [target] as [plan] says: copied
    // next to the game first, then moved in (GameFiles). On failure or
    // cancel the game is as it was and the exception is thrown.
    static void install(Source source, Found found, GameFiles.Plan plan, boolean withSaves, File target, Progress progress) throws IOException {
        if (!target.isDirectory() && !target.mkdirs()) {
            throw new IOException("can't make " + target);
        }
        // An earlier import's leftovers: finished, or its copies removed.
        GameFiles.finish(target);

        Map<String, String> installed = GameFiles.installedGameFiles(target);
        List<Entry> wanted = wanted(found, withSaves);
        Map<Entry, String> pathOf = new HashMap<>();
        for (Entry entry : wanted) {
            pathOf.put(entry, entry.path.substring(found.root.length()));
        }

        GameFiles.StagingSink sink = new GameFiles.StagingSink(target, installed, pathOf, plan.bytesToRead, (done, total, path) -> {
            if (progress.isCancelled()) {
                throw new CancelledException();
            }
            progress.update(done, total, path);
        });
        try {
            source.read(wanted, sink);
        } catch (IOException | RuntimeException e) {
            sink.abandon();
            GameFiles.deleteRecursively(new File(target, GameFiles.STAGING));
            throw e;
        }

        GameFiles.commit(target, sink.staged, plan.removed, installed);
        // An earlier version's mark of a copy left in part.
        //noinspection ResultOfMethodCallIgnored
        new File(target, INCOMPLETE_MARK).delete();
    }

    // The source's path inside [target], never outside it ("../" in an
    // archive).
    static File targetFile(File target, String path) throws IOException {
        File file = new File(target, path);
        String root = target.getCanonicalPath() + File.separator;
        if (!file.getCanonicalPath().startsWith(root)) {
            throw new IOException("bad path " + path);
        }
        return file;
    }

    // A computer's config may name the game's files with full Windows paths
    // ("C:\Games\Fallout 2\master.dat"): the copied ones are next to it.
    static void fixConfig(File config) throws IOException {
        if (!config.isFile()) {
            return;
        }

        Map<String, String> defaults = new HashMap<>();
        defaults.put("master_dat", "master.dat");
        defaults.put("critter_dat", "critter.dat");
        defaults.put("master_patches", "data");
        defaults.put("critter_patches", "data");

        String text = new String(readFile(config), StandardCharsets.ISO_8859_1);
        String newline = text.contains("\r\n") ? "\r\n" : "\n";
        String[] lines = text.split("\r?\n", -1);
        boolean changed = false;
        for (int index = 0; index < lines.length; index++) {
            int equals = lines[index].indexOf('=');
            if (equals == -1) {
                continue;
            }
            String key = lines[index].substring(0, equals).trim().toLowerCase(Locale.ROOT);
            String value = lines[index].substring(equals + 1).trim();
            if (defaults.containsKey(key) && (value.contains(":") || value.startsWith("\\") || value.startsWith("/"))) {
                lines[index] = key + "=" + defaults.get(key);
                changed = true;
            }
        }

        if (changed) {
            writeFile(config, String.join(newline, lines).getBytes(StandardCharsets.ISO_8859_1));
        }
    }

    private static byte[] readFile(File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[16384];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return out.toByteArray();
        }
    }

    private static void writeFile(File file, byte[] data) throws IOException {
        try (OutputStream out = new FileOutputStream(file)) {
            out.write(data);
        }
    }
}
