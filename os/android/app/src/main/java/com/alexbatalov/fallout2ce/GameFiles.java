package com.alexbatalov.fallout2ce;

import java.io.ByteArrayOutputStream;
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
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

// The installed game's folder (the app's files folder, laid out as on a
// computer) and putting a game into it: the first install and "Replace game"
// are one operation. The game's files become the source's; the player's
// (saves, settings in fallout2.cfg) and the app's own stay.
//
// Safely: the source's files are copied into a staging folder next to the
// game first (files already there with the same contents aren't), the game
// is untouched until all are there; then a journal of moves and removals is
// written and carried out - finished on the next start if the app dies in
// between (`finish`).
final class GameFiles {
    static final String STAGING = ".import-staging";
    static final String JOURNAL = ".import-commit";
    private static final String JOURNAL_TEMP = ".import-commit.tmp";
    static final String CONFIG = "fallout2.cfg";

    private GameFiles() {
    }

    // The game's files (replaced and removed by "Replace game"): all but the
    // saves, the app's own (ce.dat, logs, the import's own files) and
    // screenshots. [path]: from the game's folder, "/" between folders.
    static boolean isGameFile(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        if (lower.startsWith("data/savegame/") || lower.equals("ce.dat") || lower.startsWith("ce.dat/") || lower.startsWith(".import")) {
            return false;
        }
        if (lower.indexOf('/') == -1) {
            return !(lower.endsWith(".log")
                || lower.endsWith(".log.old")
                || (lower.startsWith("scr") && (lower.endsWith(".bmp") || lower.endsWith(".png"))));
        }
        return true;
    }

    // Fallout 2's own files (not mods'): kept when the source has no
    // master.dat and critter.dat (only the mods are replaced).
    static boolean isOriginalFile(String path) {
        String lower = path.toLowerCase(Locale.ROOT);
        return lower.equals("master.dat")
            || lower.equals("critter.dat")
            || lower.equals("patch000.dat")
            || lower.startsWith("sound/music/")
            || lower.startsWith("data/sound/music/");
    }

    // The game's files in [target] by their lower case paths.
    static Map<String, String> installedGameFiles(File target) {
        Map<String, String> files = new HashMap<>();
        listFiles(target, "", files);
        return files;
    }

    private static void listFiles(File folder, String prefix, Map<String, String> files) {
        File[] children = folder.listFiles();
        if (children == null) {
            return;
        }
        for (File child : children) {
            String path = prefix + child.getName();
            if (child.isDirectory()) {
                if (isGameFile(path + "/")) {
                    listFiles(child, path + "/", files);
                }
            } else if (isGameFile(path)) {
                files.put(path.toLowerCase(Locale.ROOT), path);
            }
        }
    }

    // What putting the source's files in does to the game.
    static final class Plan {
        // The source's files to put in ("/" paths from the game's folder).
        final List<String> paths = new ArrayList<>();
        final List<Long> sizes = new ArrayList<>();
        // Installed game files the source doesn't have.
        final List<String> removed = new ArrayList<>();
        int added;
        int replaced;
        // Same size: compared while copying, likely the same.
        int same;
        // Bytes to write if the files of the same size are the same.
        long bytesToWrite;
        long bytesToRead;
        final List<String> modsAdded = new ArrayList<>();
        final List<String> modsRemoved = new ArrayList<>();
    }

    // [paths] / [sizes]: the source's files; [installed]: `installedGameFiles`
    // of [target]. [modsOnly]: the original files stay (the source has no
    // master.dat).
    static Plan plan(List<String> paths, List<Long> sizes, File target, Map<String, String> installed, boolean modsOnly) {
        Plan plan = new Plan();
        Set<String> source = new HashSet<>();
        for (int index = 0; index < paths.size(); index++) {
            String path = paths.get(index);
            String lower = path.toLowerCase(Locale.ROOT);
            source.add(lower);
            plan.paths.add(path);
            plan.sizes.add(sizes.get(index));
            plan.bytesToRead += sizes.get(index);

            String installedPath = installed.get(lower);
            Long size = installedPath != null ? new File(target, installedPath).length() : null;
            if (size == null) {
                plan.added++;
                plan.bytesToWrite += sizes.get(index);
                if (isMod(lower)) {
                    plan.modsAdded.add(modName(path));
                }
            } else if (size.longValue() != sizes.get(index)) {
                plan.replaced++;
                plan.bytesToWrite += sizes.get(index);
            } else {
                plan.same++;
            }
        }

        for (Map.Entry<String, String> file : installed.entrySet()) {
            String lower = file.getKey();
            if (source.contains(lower) || (modsOnly && isOriginalFile(lower)) || lower.equals(CONFIG)) {
                continue;
            }
            plan.removed.add(file.getValue());
            if (isMod(lower)) {
                plan.modsRemoved.add(modName(file.getValue()));
            }
        }
        return plan;
    }

    private static boolean isMod(String lower) {
        return lower.startsWith("mods/") && lower.endsWith(".dat") && lower.indexOf('/', 5) == -1;
    }

    private static String modName(String path) {
        return path.substring(5, path.length() - 4);
    }

    interface Progress {
        void update(long done, long total, String path) throws IOException;
    }

    // Puts the source's files into [staging] (paths as in [plan]); a file
    // the game has with the same size is compared while read and only
    // written once it differs. Its `staged` - the paths written.
    static final class StagingSink implements GameImport.EntrySink {
        private final File target;
        private final File staging;
        private final Map<String, String> installed;
        private final Map<GameImport.Entry, String> pathOf;
        private final long total;
        private final Progress progress;
        final List<String> staged = new ArrayList<>();

        private String path;
        private InputStream existing;
        private OutputStream out;
        // While comparing: bytes found the same.
        private long same;
        private long done;
        private long lastUpdate;
        private byte[] compare = new byte[256 * 1024];

        StagingSink(File target, Map<String, String> installed, Map<GameImport.Entry, String> pathOf, long total, Progress progress) {
            this.target = target;
            this.staging = new File(target, STAGING);
            this.installed = installed;
            this.pathOf = pathOf;
            this.total = total;
            this.progress = progress;
        }

        @Override
        public void begin(GameImport.Entry entry) throws IOException {
            path = pathOf.get(entry);
            same = 0;
            String existingPath = installed.get(path.toLowerCase(Locale.ROOT));
            File existingFile = existingPath != null ? new File(target, existingPath) : null;
            if (existingFile != null && existingFile.length() == entry.size) {
                existing = new FileInputStream(existingFile);
            } else {
                startWriting(null);
            }
        }

        @Override
        public void data(byte[] buffer, int length) throws IOException {
            int offset = 0;
            if (existing != null) {
                // The same so far: compare this piece.
                if (compare.length < length) {
                    compare = new byte[length];
                }
                int compared = readFully(existing, compare, length);
                while (offset < compared && compare[offset] == buffer[offset]) {
                    offset++;
                }
                if (offset == length) {
                    same += length;
                    advance(length);
                    return;
                }
                // Differs: write what was the same, then the rest.
                same += offset;
                startWriting(existingPathFile());
            }
            out.write(buffer, offset, length - offset);
            advance(length);
        }

        @Override
        public void end(GameImport.Entry entry) throws IOException {
            if (existing != null) {
                // Read to its end the same: nothing to write. A longer one
                // (grown since the listing) is written anew.
                boolean longer = existing.read() != -1;
                existing.close();
                existing = null;
                if (!longer) {
                    return;
                }
                File source = existingPathFile();
                startWriting(null);
                copyPrefix(source, same);
            }
            ((FileOutputStream) out).getFD().sync();
            out.close();
            out = null;
            staged.add(path);
        }

        private File existingPathFile() {
            return new File(target, installed.get(path.toLowerCase(Locale.ROOT)));
        }

        // Opens the staged file; after a comparison, with the [same] bytes
        // of [prefixSource] first.
        private void startWriting(File prefixSource) throws IOException {
            if (existing != null) {
                existing.close();
                existing = null;
            }
            File file = GameImport.targetFile(staging, path);
            File folder = file.getParentFile();
            if (folder != null && !folder.isDirectory() && !folder.mkdirs()) {
                throw new IOException("can't make " + folder);
            }
            out = new FileOutputStream(file);
            if (prefixSource != null) {
                copyPrefix(prefixSource, same);
            }
        }

        private void copyPrefix(File source, long length) throws IOException {
            try (InputStream in = new FileInputStream(source)) {
                long remaining = length;
                while (remaining > 0) {
                    int read = in.read(compare, 0, (int) Math.min(compare.length, remaining));
                    if (read <= 0) {
                        throw new IOException("can't read " + source);
                    }
                    out.write(compare, 0, read);
                    remaining -= read;
                }
            }
        }

        private void advance(long length) throws IOException {
            done += length;
            long now = System.currentTimeMillis();
            if (now - lastUpdate > 100) {
                lastUpdate = now;
                progress.update(done, total, path);
            }
        }

        // After a failure or cancel: open files closed (the staging folder
        // is removed by the caller).
        void abandon() {
            try {
                if (existing != null) {
                    existing.close();
                }
                if (out != null) {
                    out.close();
                }
            } catch (IOException ignored) {
            }
            existing = null;
            out = null;
        }
    }

    private static int readFully(InputStream in, byte[] buffer, int length) throws IOException {
        int total = 0;
        while (total < length) {
            int read = in.read(buffer, total, length - total);
            if (read <= 0) {
                break;
            }
            total += read;
        }
        return total;
    }

    // Moves [staged] (in the staging folder) in and removes [removed]: the
    // journal first, then `finish`. fallout2.cfg gets the player's settings
    // of the one it replaces (`mergeSettings`).
    static void commit(File target, List<String> staged, List<String> removed, Map<String, String> installed) throws IOException {
        File staging = new File(target, STAGING);

        // The new config: a computer's paths fixed, the player's settings of
        // the current one.
        File currentConfig = new File(target, CONFIG);
        for (String path : staged) {
            if (path.equalsIgnoreCase(CONFIG)) {
                File newConfig = new File(staging, path);
                GameImport.fixConfig(newConfig);
                if (currentConfig.isFile()) {
                    writeText(newConfig, mergeSettings(readText(newConfig), readText(currentConfig)));
                }
            }
        }

        StringBuilder journal = new StringBuilder();
        for (String path : staged) {
            // An installed file's own name (its case) is kept.
            String existing = installed.get(path.toLowerCase(Locale.ROOT));
            journal.append("M\t").append(path).append('\t').append(existing != null ? existing : path).append('\n');
        }
        for (String path : removed) {
            journal.append("D\t").append(path).append('\n');
        }

        File temp = new File(target, JOURNAL_TEMP);
        try (FileOutputStream out = new FileOutputStream(temp)) {
            out.write(journal.toString().getBytes(StandardCharsets.UTF_8));
            out.getFD().sync();
        }
        if (!temp.renameTo(new File(target, JOURNAL))) {
            throw new IOException("can't write the journal");
        }
        finish(target);
    }

    // Carries out the journal if there is one (again after a crash: each
    // step can be repeated), then removes the staging folder - also one left
    // by an import that didn't get to its journal.
    static void finish(File target) throws IOException {
        File journalFile = new File(target, JOURNAL);
        File staging = new File(target, STAGING);
        if (journalFile.isFile()) {
            String journal = readText(journalFile);
            for (String line : journal.split("\n")) {
                String[] parts = line.split("\t");
                if (parts.length == 3 && parts[0].equals("M")) {
                    File from = new File(staging, parts[1]);
                    File to = GameImport.targetFile(target, parts[2]);
                    if (!from.isFile()) {
                        // Moved already.
                        continue;
                    }
                    File folder = to.getParentFile();
                    if (folder != null && !folder.isDirectory() && !folder.mkdirs()) {
                        throw new IOException("can't make " + folder);
                    }
                    if (!from.renameTo(to)) {
                        //noinspection ResultOfMethodCallIgnored
                        to.delete();
                        if (!from.renameTo(to)) {
                            throw new IOException("can't move " + parts[1]);
                        }
                    }
                } else if (parts.length == 2 && parts[0].equals("D")) {
                    File file = GameImport.targetFile(target, parts[1]);
                    //noinspection ResultOfMethodCallIgnored
                    file.delete();
                    removeEmptyFolders(target, file.getParentFile());
                }
            }
            if (!journalFile.delete()) {
                throw new IOException("can't remove the journal");
            }
        }
        //noinspection ResultOfMethodCallIgnored
        new File(target, JOURNAL_TEMP).delete();
        deleteRecursively(staging);
    }

    static boolean isCommitPending(File target) {
        return new File(target, JOURNAL).isFile();
    }

    // Folders left empty by removed files, up to the game's folder.
    private static void removeEmptyFolders(File target, File folder) {
        while (folder != null && !folder.equals(target)) {
            String[] children = folder.list();
            if (children == null || children.length != 0 || !folder.delete()) {
                return;
            }
            folder = folder.getParentFile();
        }
    }

    static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteRecursively(child);
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }

    // The new game's config with the player's settings of the old one:
    // every section's values but [system] (the language and the game's
    // files) and the music folders (the new game's). Keys and sections the
    // new one lacks are added; its lines, comments and order stay.
    static String mergeSettings(String newText, String oldText) {
        Map<String, LinkedHashMap<String, String>> old = parseConfig(oldText);
        String newline = newText.contains("\r\n") ? "\r\n" : "\n";
        String[] lines = newText.split("\r?\n", -1);

        StringBuilder merged = new StringBuilder();
        Set<String> done = new HashSet<>();
        Set<String> sections = new HashSet<>();
        String section = "";
        for (int index = 0; index < lines.length; index++) {
            String line = lines[index];
            String trimmed = line.trim();
            boolean last = index == lines.length - 1;
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                appendMissing(merged, section, old, done, newline);
                section = trimmed.substring(1, trimmed.length() - 1).trim().toLowerCase(Locale.ROOT);
                sections.add(section);
            } else {
                int equals = line.indexOf('=');
                if (equals != -1 && !trimmed.startsWith(";") && !trimmed.startsWith("#")) {
                    String key = line.substring(0, equals).trim().toLowerCase(Locale.ROOT);
                    Map<String, String> oldSection = old.get(section);
                    if (isPlayerSetting(section, key) && oldSection != null && oldSection.containsKey(key)) {
                        line = line.substring(0, equals + 1) + oldSection.get(key);
                    }
                    done.add(section + "\n" + key);
                }
            }
            if (last && line.isEmpty()) {
                // The text's own last newline.
                break;
            }
            merged.append(line).append(newline);
        }
        appendMissing(merged, section, old, done, newline);

        for (Map.Entry<String, LinkedHashMap<String, String>> oldSection : old.entrySet()) {
            String name = oldSection.getKey();
            if (sections.contains(name) || name.isEmpty()) {
                continue;
            }
            StringBuilder keys = new StringBuilder();
            for (Map.Entry<String, String> setting : oldSection.getValue().entrySet()) {
                if (isPlayerSetting(name, setting.getKey())) {
                    keys.append(setting.getKey()).append('=').append(setting.getValue()).append(newline);
                }
            }
            if (keys.length() != 0) {
                merged.append(newline).append('[').append(name).append(']').append(newline).append(keys);
            }
        }
        return merged.toString();
    }

    private static void appendMissing(StringBuilder merged, String section, Map<String, LinkedHashMap<String, String>> old, Set<String> done, String newline) {
        Map<String, String> oldSection = old.get(section);
        if (oldSection == null || section.isEmpty()) {
            return;
        }
        // Before the blank lines ending the section.
        int end = merged.length();
        while (end >= newline.length() * 2 && merged.substring(end - newline.length() * 2, end).equals(newline + newline)) {
            end -= newline.length();
        }
        StringBuilder missing = new StringBuilder();
        for (Map.Entry<String, String> setting : oldSection.entrySet()) {
            if (isPlayerSetting(section, setting.getKey()) && !done.contains(section + "\n" + setting.getKey())) {
                missing.append(setting.getKey()).append('=').append(setting.getValue()).append(newline);
            }
        }
        merged.insert(end, missing);
    }

    private static boolean isPlayerSetting(String section, String key) {
        if (section.equals("system")) {
            return false;
        }
        return !(section.equals("sound") && (key.equals("music_path1") || key.equals("music_path2")));
    }

    // Sections (lower case) -> keys (lower case) -> values.
    private static Map<String, LinkedHashMap<String, String>> parseConfig(String text) {
        Map<String, LinkedHashMap<String, String>> config = new LinkedHashMap<>();
        String section = "";
        for (String line : text.split("\r?\n")) {
            String trimmed = line.trim();
            if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
                section = trimmed.substring(1, trimmed.length() - 1).trim().toLowerCase(Locale.ROOT);
                continue;
            }
            int equals = line.indexOf('=');
            if (equals == -1 || trimmed.startsWith(";") || trimmed.startsWith("#")) {
                continue;
            }
            LinkedHashMap<String, String> keys = config.get(section);
            if (keys == null) {
                keys = new LinkedHashMap<>();
                config.put(section, keys);
            }
            keys.put(line.substring(0, equals).trim().toLowerCase(Locale.ROOT), line.substring(equals + 1));
        }
        return config;
    }

    static String readText(File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[16384];
            int read;
            while ((read = in.read(buffer)) > 0) {
                out.write(buffer, 0, read);
            }
            return new String(out.toByteArray(), StandardCharsets.ISO_8859_1);
        }
    }

    static void writeText(File file, String text) throws IOException {
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.ISO_8859_1));
            out.getFD().sync();
        }
    }
}
