package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

// GameSaves' export and import of saves on files of a temporary folder.
public class GameSavesTest {
    @Rule
    public TemporaryFolder temporary = new TemporaryFolder();

    private File game;
    private File records;

    // Compositions compared as equal or not (the real rule is native).
    private static final GameSaves.Comparer COMPARER = (made, now) -> made.equals(now) ? "0" : "1\n1\tmods/rpu.dat";

    private static final GameSaves.Progress NO_PROGRESS = (done, total) -> {
    };

    // A SAVE.DAT: its header (big-endian), [tag] in the description makes it
    // one of its own.
    static byte[] saveDat(String name, String tag, int day, int month, int year, int gameTime) {
        byte[] data = new byte[2000];
        byte[] signature = "FALLOUT SAVE FILE".getBytes(StandardCharsets.ISO_8859_1);
        System.arraycopy(signature, 0, data, 0, signature.length);
        data[25] = 1;
        data[27] = 2;
        data[28] = 'R';
        byte[] nameBytes = name.getBytes(StandardCharsets.ISO_8859_1);
        System.arraycopy(nameBytes, 0, data, 29, nameBytes.length);
        byte[] tagBytes = tag.getBytes(StandardCharsets.ISO_8859_1);
        System.arraycopy(tagBytes, 0, data, 61, tagBytes.length);
        data[91] = (byte) (day >> 8);
        data[92] = (byte) day;
        data[93] = (byte) (month >> 8);
        data[94] = (byte) month;
        data[95] = (byte) (year >> 8);
        data[96] = (byte) year;
        data[107] = (byte) (gameTime >> 24);
        data[108] = (byte) (gameTime >> 16);
        data[109] = (byte) (gameTime >> 8);
        data[110] = (byte) gameTime;
        return data;
    }

    private void write(File file, byte[] data) throws IOException {
        file.getParentFile().mkdirs();
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(data);
        }
    }

    private void write(String path, String text) throws IOException {
        write(new File(game, path), text.getBytes(StandardCharsets.ISO_8859_1));
    }

    // The game's save in [slot] (0-based) with its record.
    private void installSave(int slot, String tag, long order, String composition) throws IOException {
        File folder = new File(GameSaves.savesFolder(game), GameSaves.slotName(slot));
        write(new File(folder, "SAVE.DAT"), saveDat("Chosen", tag, 1, 2, 2025, 100));
        write(new File(folder, "AUTOMAP.SAV"), ("map " + tag).getBytes(StandardCharsets.ISO_8859_1));
        Map<Integer, GameSaves.Record> known = GameSaves.readRecords(records);
        GameSaves.Record record = new GameSaves.Record();
        record.id = GameSaves.saveId(new File(folder, "SAVE.DAT"));
        record.created = order / 1000000;
        record.order = order;
        record.composition = composition;
        known.put(slot, record);
        GameSaves.writeRecords(records, known);
    }

    @Before
    public void makeGame() throws IOException {
        game = temporary.newFolder("game");
        records = new File(temporary.getRoot(), "saves.txt");
        write("master.dat", "MASTER");
        write("critter.dat", "CRITTER");
        write("mods/rpu.dat", "RPU");
        // One quick page: slots 11-20.
        write("fallout2.cfg", "[system]\nlanguage=english\n\n[ui]\nauto_quick_save=1\nauto_quick_save_page=1\n");
    }

    // The bytes of a zip as a source.
    private GameFilesTest.MemorySource unzip(byte[] zip) throws IOException {
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        try (ZipInputStream in = new ZipInputStream(new ByteArrayInputStream(zip))) {
            ZipEntry entry;
            while ((entry = in.getNextEntry()) != null) {
                ByteArrayOutputStream data = new ByteArrayOutputStream();
                byte[] buffer = new byte[4096];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    data.write(buffer, 0, read);
                }
                source.files.put(entry.getName(), data.toByteArray());
            }
        }
        return source;
    }

    @Test
    public void identityIsTheEngines() {
        // src/save_records.cc: FNV-1a 64 of the first 4 KB, then the size.
        byte[] data = "abc".getBytes(StandardCharsets.ISO_8859_1);
        assertEquals("e71fa2190541574b-3", GameSaves.saveId(data, data.length, data.length));
    }

    @Test
    public void quickRanges() {
        assertTrue(Arrays.equals(new int[] { 10, 10 }, GameSaves.quickRange("[ui]\nauto_quick_save=1\nauto_quick_save_page=1\n", null)));
        // CE's default: none.
        assertTrue(Arrays.equals(new int[] { 10, 0 }, GameSaves.quickRange("[ui]\n", null)));
        // sfall's when the CE config doesn't say.
        assertTrue(Arrays.equals(new int[] { 20, 20 }, GameSaves.quickRange(null, "[Misc]\nAutoQuickSave=2 ; pages\nAutoQuickSavePage=2\n")));
    }

    @Test
    public void exportThenImportElsewhere() throws IOException {
        installSave(0, "manual", 1700000000000001L, "*");
        installSave(10, "quick", 1700000100000001L, "master.dat=1:G");
        ByteArrayOutputStream zip = new ByteArrayOutputStream();
        assertEquals(2, GameSaves.export(game, records, "master.dat=1:G", zip, NO_PROGRESS));

        // Another phone, its own save in slot 1 and in the first quick slot.
        File first = game;
        File firstRecords = records;
        game = temporary.newFolder("other");
        records = new File(temporary.getRoot(), "other.txt");
        write("fallout2.cfg", "[ui]\nauto_quick_save=1\nauto_quick_save_page=1\n");
        installSave(0, "theirs", 1600000000000000L, "");
        installSave(10, "their quick", 1600000000000001L, "");

        GameSaves.Found found = GameSaves.find(unzip(zip.toByteArray()), game, "master.dat=1:G", COMPARER);
        assertTrue(found.withInfo);
        assertEquals(2, found.saves.size());
        // Newest first: the quick one; "*" was the exporting game's.
        GameSaves.FoundSave quick = found.saves.get(0);
        assertTrue(quick.quick);
        assertEquals(1700000100000001L, quick.record.order);
        assertEquals(GameSaves.SAME, quick.level);
        assertEquals(GameSaves.SAME, found.saves.get(1).level);

        // Added: free manual slots (2, 3), quick saves kept as saves of their own.
        GameSaves.Plan plan = GameSaves.plan(found, game, false);
        assertEquals(Arrays.asList(1, 2), plan.slots);
        GameSaves.apply(unzip(zip.toByteArray()), plan, game, records, NO_PROGRESS);
        File saves = GameSaves.savesFolder(game);
        assertTrue(new File(saves, "SLOT02/SAVE.DAT").isFile());
        assertTrue(new File(saves, "SLOT03/AUTOMAP.SAV").isFile());
        assertTrue(new File(saves, "SLOT01/SAVE.DAT").isFile());
        Map<Integer, GameSaves.Record> known = GameSaves.readRecords(records);
        assertEquals(1700000100000001L, known.get(1).order);
        assertEquals("master.dat=1:G", known.get(2).composition);
        assertEquals(4, known.size());

        // Again: both are there already.
        found = GameSaves.find(unzip(zip.toByteArray()), game, "master.dat=1:G", COMPARER);
        plan = GameSaves.plan(found, game, false);
        assertEquals(2, plan.skipped);
        assertTrue(plan.saves.isEmpty());

        // Replacing all: only these two, the quick one in the quick slots.
        plan = GameSaves.plan(found, game, true);
        assertEquals(Arrays.asList(10, 0), plan.slots);
        GameSaves.apply(unzip(zip.toByteArray()), plan, game, records, NO_PROGRESS);
        assertEquals(new java.util.TreeSet<>(Arrays.asList(0, 10)), GameSaves.installedSaves(game).keySet());
        assertEquals(2, GameSaves.readRecords(records).size());
        assertFalse(new File(saves, ".ce-import").exists());
        assertFalse(new File(saves, ".ce-import-old").exists());
        game = first;
        records = firstRecords;
    }

    @Test
    public void aComputersSavesOfTheWholeGame() throws IOException {
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        source.files.put("Fallout 2/master.dat", "MASTER".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout 2/critter.dat", "CRITTER".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout 2/mods/rpu.dat", "RPU".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout 2/ddraw.ini", "[Misc]\nAutoQuickSave=1\nAutoQuickSavePage=2\n".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout 2/data/SAVEGAME/SLOT21/SAVE.DAT", saveDat("A", "late", 3, 3, 2025, 10));
        source.files.put("Fallout 2/data/SAVEGAME/SLOT21/CE-META.TXT", "1 1 1 1".getBytes(StandardCharsets.ISO_8859_1));
        source.files.put("Fallout 2/data/SAVEGAME/SLOT02/SAVE.DAT", saveDat("A", "early", 1, 3, 2025, 900));
        source.files.put("Fallout 2/backup/data/SAVEGAME/SLOT01/SAVE.DAT", saveDat("A", "old", 1, 1, 2020, 1));

        GameSaves.Found found = GameSaves.find(source, game, "now", COMPARER);
        assertEquals("Fallout 2/data/SAVEGAME/", found.root);
        assertFalse(found.withInfo);
        // The same archives as the installed game: made with it.
        assertTrue(found.sameGame);
        assertEquals(2, found.saves.size());
        GameSaves.FoundSave late = found.saves.get(0);
        assertEquals("late", late.header.description);
        assertTrue(late.quick);
        assertEquals(GameSaves.SAME, late.level);
        assertTrue(late.record.order > found.saves.get(1).record.order);
        assertEquals(0, late.record.created);
        // The port's old file isn't taken along.
        assertEquals(1, late.files.size());
    }

    @Test
    public void bareSlotsAreUnknown() throws IOException {
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        source.files.put("SLOT05/SAVE.DAT", saveDat("B", "bare", 1, 1, 2024, 1));
        GameSaves.Found found = GameSaves.find(source, game, "now", COMPARER);
        assertEquals("", found.root);
        assertEquals(GameSaves.UNKNOWN, found.saves.get(0).level);
        assertFalse(found.saves.get(0).quick);

        assertNull(GameSaves.find(new GameFilesTest.MemorySource().put("readme.txt", "x"), game, "now", COMPARER));
    }

    @Test
    public void noRoom() throws IOException {
        // 990 manual slots taken (the 10 quick ones aren't for adding).
        for (int slot = 0; slot < GameSaves.TOTAL_SLOTS; slot++) {
            if (slot < 10 || slot >= 20) {
                new File(GameSaves.savesFolder(game), GameSaves.slotName(slot)).mkdirs();
            }
        }
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        source.files.put("SLOT05/SAVE.DAT", saveDat("B", "bare", 1, 1, 2024, 1));
        GameSaves.Found found = GameSaves.find(source, game, "now", COMPARER);
        try {
            GameSaves.plan(found, game, false);
            fail();
        } catch (GameSaves.NoRoomException e) {
            assertEquals(1, e.needed);
            assertEquals(0, e.free);
        }
    }

    @Test
    public void journalIsFinishedAfterACrash() throws IOException {
        installSave(0, "old", 1, "");
        File saves = GameSaves.savesFolder(game);
        write(new File(saves, ".ce-import/SLOT03/SAVE.DAT"), saveDat("C", "new", 1, 1, 2024, 1));
        write(new File(saves, ".ce-import-commit"), "O\tSLOT01\nM\tSLOT03\n".getBytes(StandardCharsets.UTF_8));
        assertTrue(GameSaves.isCommitPending(game));
        GameSaves.finish(game);
        assertFalse(new File(saves, "SLOT01").exists());
        assertTrue(new File(saves, "SLOT03/SAVE.DAT").isFile());
        assertFalse(GameSaves.isCommitPending(game));
        assertFalse(new File(saves, ".ce-import-old").exists());
        GameSaves.finish(game);
        assertTrue(new File(saves, "SLOT03/SAVE.DAT").isFile());
    }

    @Test
    public void severalSaveFoldersAtOneDepth() throws IOException {
        GameFilesTest.MemorySource source = new GameFilesTest.MemorySource();
        source.files.put("a/SLOT01/SAVE.DAT", saveDat("A", "a", 1, 1, 2024, 1));
        source.files.put("b/SLOT01/SAVE.DAT", saveDat("B", "b", 1, 1, 2024, 1));
        try {
            GameSaves.find(source, game, "now", COMPARER);
            fail();
        } catch (GameImport.SeveralGamesException e) {
            assertEquals(new ArrayList<>(Arrays.asList("a/", "b/")), e.roots);
        }
    }
}
