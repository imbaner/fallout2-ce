package com.alexbatalov.fallout2ce;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

// GameImport's finding of the game and GameFiles' putting it into the game's
// folder, on files of a temporary folder.
public class GameFilesTest {
    @Rule
    public TemporaryFolder temporary = new TemporaryFolder();

    // A folder or archive in memory, its data given in [chunk] pieces.
    static final class MemorySource extends GameImport.Source {
        final Map<String, byte[]> files = new LinkedHashMap<>();
        int chunk = 1 << 20;
        // Throws in the middle of reading the file of this name.
        String failOn;

        MemorySource put(String path, String text) {
            files.put(path, text.getBytes(StandardCharsets.ISO_8859_1));
            return this;
        }

        @Override
        List<GameImport.Entry> list() {
            List<GameImport.Entry> entries = new ArrayList<>();
            for (Map.Entry<String, byte[]> file : files.entrySet()) {
                entries.add(new GameImport.Entry(file.getKey(), file.getValue().length, file.getKey(), false, false));
            }
            return entries;
        }

        @Override
        void read(List<GameImport.Entry> wanted, GameImport.EntrySink sink) throws IOException {
            for (GameImport.Entry entry : wanted) {
                byte[] data = files.get(entry.path);
                sink.begin(entry);
                for (int offset = 0; offset < data.length; offset += chunk) {
                    if (entry.path.equals(failOn) && offset > 0) {
                        throw new IOException("broken");
                    }
                    byte[] piece = Arrays.copyOfRange(data, offset, Math.min(data.length, offset + chunk));
                    sink.data(piece, piece.length);
                }
                sink.end(entry);
            }
        }
    }

    static final GameImport.Progress NO_PROGRESS = new GameImport.Progress() {
        @Override
        public void update(long copied, long total, String path) {
        }

        @Override
        public boolean isCancelled() {
            return false;
        }
    };

    private File game;

    private void write(String path, String text) throws IOException {
        File file = new File(game, path);
        file.getParentFile().mkdirs();
        try (FileOutputStream out = new FileOutputStream(file)) {
            out.write(text.getBytes(StandardCharsets.ISO_8859_1));
        }
    }

    private String read(String path) throws IOException {
        return GameFiles.readText(new File(game, path));
    }

    private boolean exists(String path) {
        return new File(game, path).exists();
    }

    // An installed game: Fallout 2's files, two mods, the player's config,
    // a save, the app's own files.
    private void installGame() throws IOException {
        game = temporary.newFolder("game");
        write("master.dat", "MASTER");
        write("critter.dat", "CRITTER");
        write("sound/music/01.acm", "MUSIC");
        write("mods/rpu.dat", "RPU 1");
        write("mods/old_mod.dat", "OLD");
        write("ddraw.ini", "[Main]\nOld=1\n");
        write("fallout2.cfg", "[system]\nlanguage=russian\n\n[preferences]\ngame_difficulty=2\nbrightness=1.2\n\n[enhancements]\nmain_menu_continue=1\n");
        write("data/SAVEGAME/SLOT01/SAVE.DAT", "SAVE");
        write("ce.dat/font.ttf", "FONT");
        write("actions.log", "LOG");
    }

    private void install(MemorySource source, boolean modsOnly, boolean withSaves) throws IOException {
        GameImport.Found found = GameImport.find(source, modsOnly);
        GameFiles.Plan plan = GameImport.plan(found, game, withSaves);
        GameImport.install(source, found, plan, withSaves, game, NO_PROGRESS);
    }

    @Test
    public void gameFiles() {
        assertTrue(GameFiles.isGameFile("master.dat"));
        assertTrue(GameFiles.isGameFile("mods/rpu.dat"));
        assertTrue(GameFiles.isGameFile("data/proto/items/00000001.pro"));
        assertFalse(GameFiles.isGameFile("data/SAVEGAME/SLOT01/SAVE.DAT"));
        assertFalse(GameFiles.isGameFile("ce.dat/fonts/a.ttf"));
        assertFalse(GameFiles.isGameFile("actions.log"));
        assertFalse(GameFiles.isGameFile("touch.log.old"));
        assertFalse(GameFiles.isGameFile("scr00001.bmp"));
        assertFalse(GameFiles.isGameFile(".import-staging/master.dat"));
        assertTrue(GameFiles.isOriginalFile("MASTER.DAT"));
        assertTrue(GameFiles.isOriginalFile("sound/music/07desert.acm"));
        assertFalse(GameFiles.isOriginalFile("mods/rpu.dat"));
    }

    @Test
    public void settingsCarryOver() {
        String newConfig = "[system]\r\nlanguage=english\r\nmaster_dat=master.dat\r\n\r\n[preferences]\r\ngame_difficulty=1\r\n\r\n[sound]\r\nmusic_path1=sound\\music\\\r\nmaster_volume=22000\r\n";
        String oldConfig = "[system]\nlanguage=russian\n\n[preferences]\ngame_difficulty=2\nbrightness=1.200000\n\n[sound]\nmusic_path1=old\\music\\\nmaster_volume=10000\n\n[enhancements]\nmain_menu_continue=1\n";
        String merged = GameFiles.mergeSettings(newConfig, oldConfig);
        assertEquals("[system]\r\nlanguage=english\r\nmaster_dat=master.dat\r\n\r\n"
                + "[preferences]\r\ngame_difficulty=2\r\nbrightness=1.200000\r\n\r\n"
                + "[sound]\r\nmusic_path1=sound\\music\\\r\nmaster_volume=10000\r\n"
                + "\r\n[enhancements]\r\nmain_menu_continue=1\r\n",
            merged);
    }

    @Test
    public void findsTheGameAtAnyDepth() throws IOException {
        MemorySource source = new MemorySource()
            .put("Игры/Fallout 2/master.dat", "M")
            .put("Игры/Fallout 2/critter.dat", "C")
            .put("Игры/Fallout 2/backup/master.dat", "M")
            .put("Игры/Fallout 2/backup/critter.dat", "C")
            .put("Игры/Fallout 2/Fallout II Community Edition.app/Contents/MacOS/x", "X")
            .put("Игры/Fallout 2/fallout2.exe", "X")
            .put("Игры/Fallout 2/data/._x", "X")
            .put("__MACOSX/Игры/._Fallout 2", "X");
        GameImport.Found found = GameImport.find(source, false);
        assertEquals("Игры/Fallout 2/", found.root);
        List<String> paths = new ArrayList<>();
        for (GameImport.Entry entry : found.files) {
            paths.add(entry.path.substring(found.root.length()));
        }
        assertEquals(Arrays.asList("master.dat", "critter.dat", "backup/master.dat", "backup/critter.dat"), paths);
        assertFalse(found.modsOnly);
    }

    @Test
    public void severalGamesAtOneDepth() throws IOException {
        MemorySource source = new MemorySource()
            .put("a/master.dat", "M").put("a/critter.dat", "C")
            .put("b/master.dat", "M").put("b/critter.dat", "C");
        try {
            GameImport.find(source, false);
            fail();
        } catch (GameImport.SeveralGamesException e) {
            assertEquals(Arrays.asList("a/", "b/"), e.roots);
        }
    }

    @Test
    public void modsOnlyIsFoundOnlyForReplacing() throws IOException {
        MemorySource source = new MemorySource()
            .put("build/mods/rpu.dat", "R")
            .put("build/ddraw.ini", "D");
        assertNull(GameImport.find(source, false));
        GameImport.Found found = GameImport.find(source, true);
        assertEquals("build/", found.root);
        assertTrue(found.modsOnly);
    }

    @Test
    public void firstInstall() throws IOException {
        game = new File(temporary.getRoot(), "game");
        MemorySource source = new MemorySource()
            .put("Fallout 2/master.dat", "MASTER")
            .put("Fallout 2/critter.dat", "CRITTER")
            .put("Fallout 2/fallout2.cfg", "[system]\nmaster_dat=C:\\Games\\Fallout 2\\master.dat\n")
            .put("Fallout 2/data/SAVEGAME/SLOT01/SAVE.DAT", "SAVE");
        install(source, false, true);

        assertTrue(GameImport.isInstalled(game));
        assertEquals("MASTER", read("master.dat"));
        assertEquals("SAVE", read("data/SAVEGAME/SLOT01/SAVE.DAT"));
        // A computer's path fixed.
        assertEquals("[system]\nmaster_dat=master.dat\n", read("fallout2.cfg"));
        assertFalse(exists(GameFiles.STAGING));
        assertFalse(exists(GameFiles.JOURNAL));
    }

    @Test
    public void replaceTheMods() throws IOException {
        installGame();
        long masterTime = new File(game, "master.dat").lastModified();
        MemorySource source = new MemorySource()
            .put("build/mods/rpu.dat", "RPU 2")
            .put("build/mods/new_mod.dat", "NEW")
            .put("build/ddraw.ini", "[Main]\nNew=1\n")
            .put("build/fallout2.cfg", "[system]\nlanguage=english\n\n[preferences]\ngame_difficulty=1\n")
            .put("build/data/SAVEGAME/SLOT09/SAVE.DAT", "OTHER SAVE");
        GameImport.Found found = GameImport.find(source, true);
        GameFiles.Plan plan = GameImport.plan(found, game, false);
        assertEquals(Arrays.asList("new_mod"), plan.modsAdded);
        assertEquals(Arrays.asList("old_mod"), plan.modsRemoved);
        GameImport.install(source, found, plan, false, game, NO_PROGRESS);

        // The originals stay (untouched), the mods are the new ones.
        assertEquals("MASTER", read("master.dat"));
        assertEquals(masterTime, new File(game, "master.dat").lastModified());
        assertEquals("MUSIC", read("sound/music/01.acm"));
        assertEquals("RPU 2", read("mods/rpu.dat"));
        assertEquals("NEW", read("mods/new_mod.dat"));
        assertFalse(exists("mods/old_mod.dat"));
        assertEquals("[Main]\nNew=1\n", read("ddraw.ini"));
        // The player's settings, the new game's language.
        assertEquals("[system]\nlanguage=english\n\n[preferences]\ngame_difficulty=2\nbrightness=1.2\n\n[enhancements]\nmain_menu_continue=1\n", read("fallout2.cfg"));
        // Saves and the app's files as they were; the source's saves not taken.
        assertEquals("SAVE", read("data/SAVEGAME/SLOT01/SAVE.DAT"));
        assertFalse(exists("data/SAVEGAME/SLOT09"));
        assertEquals("FONT", read("ce.dat/font.ttf"));
        assertEquals("LOG", read("actions.log"));
        assertFalse(exists(GameFiles.STAGING));
        assertTrue(GameImport.isInstalled(game));
    }

    @Test
    public void replaceTheWholeGame() throws IOException {
        installGame();
        MemorySource source = new MemorySource()
            .put("master.dat", "MASTER")
            .put("critter.dat", "CRITTR2")
            .put("mods/rpu.dat", "RPU 1");
        install(source, false, false);

        assertEquals("CRITTR2", read("critter.dat"));
        // Not in the new game: gone, the music too.
        assertFalse(exists("sound"));
        assertFalse(exists("mods/old_mod.dat"));
        assertFalse(exists("ddraw.ini"));
        // fallout2.cfg is never removed.
        assertTrue(exists("fallout2.cfg"));
        assertEquals("SAVE", read("data/SAVEGAME/SLOT01/SAVE.DAT"));
    }

    @Test
    public void sameSizeDifferentDataIsWritten() throws IOException {
        installGame();
        long rpuTime = new File(game, "mods/rpu.dat").lastModified();
        MemorySource source = new MemorySource()
            .put("mods/rpu.dat", "RPU 9")
            .put("mods/old_mod.dat", "OLD")
            .put("ddraw.ini", "[Main]\nOld=1\n");
        // Differs in the second piece: the first one's bytes written too.
        source.chunk = 3;
        install(source, true, false);
        assertEquals("RPU 9", read("mods/rpu.dat"));
        assertEquals("OLD", read("mods/old_mod.dat"));
        assertTrue(rpuTime <= new File(game, "mods/rpu.dat").lastModified());
    }

    @Test
    public void failureLeavesTheGame() throws IOException {
        installGame();
        MemorySource source = new MemorySource()
            .put("mods/rpu.dat", "RPU 2 LONGER")
            .put("mods/new_mod.dat", "NEW MOD DATA");
        source.chunk = 4;
        source.failOn = "mods/new_mod.dat";
        try {
            install(source, true, false);
            fail();
        } catch (IOException e) {
            assertEquals("broken", e.getMessage());
        }
        assertEquals("RPU 1", read("mods/rpu.dat"));
        assertTrue(exists("mods/old_mod.dat"));
        assertFalse(exists("mods/new_mod.dat"));
        assertFalse(exists(GameFiles.STAGING));
        assertTrue(GameImport.isInstalled(game));
    }

    @Test
    public void journalIsFinishedAfterACrash() throws IOException {
        installGame();
        // Moving in had started: rpu.dat moved, new_mod.dat not, old_mod.dat
        // not removed yet.
        write(GameFiles.STAGING + "/mods/new_mod.dat", "NEW");
        write("mods/rpu.dat", "RPU 2");
        write(GameFiles.JOURNAL, "M\tmods/rpu.dat\tmods/rpu.dat\nM\tmods/new_mod.dat\tmods/new_mod.dat\nD\tmods/old_mod.dat\n");
        assertFalse(GameImport.isInstalled(game));

        GameFiles.finish(game);
        assertEquals("RPU 2", read("mods/rpu.dat"));
        assertEquals("NEW", read("mods/new_mod.dat"));
        assertFalse(exists("mods/old_mod.dat"));
        assertFalse(exists(GameFiles.JOURNAL));
        assertFalse(exists(GameFiles.STAGING));
        assertTrue(GameImport.isInstalled(game));

        // Again: nothing left to do.
        GameFiles.finish(game);
        assertEquals("NEW", read("mods/new_mod.dat"));
    }
}
