// Test program for the save importer (runtime/services/save_convert.c), with no SDL and no game files. Built and driven
// by tools/test_save_convert.py (make test-save-convert), which compares its output with tools/botw_save_convert.py.
//
//   test_save_convert convert --src <folder> --bootup <Bootup.pack> --dst <new folder> [--allow-switch]
//       sc_format_load + sc_convert into <new folder>/save_0, then one line per converted file, as the Python tool
//       reports it; exit status 0 when it converted, 1 when it refused (the reason follows RESULT)
//   test_save_convert slots <save_0 folder>
//       sc_list_slots: SLOT <name> <saved (unix time)> <play seconds or -1> <has picture>
//   test_save_convert selftest <scratch folder>
//       the safety of backup, install and discard in folders made under <scratch folder> (left for the caller to
//       remove): the player's saves are never lost
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../runtime/services/save_convert.c"  // (the whole file: the test reaches its helpers)

static int convert_cmd(int argc, char **argv) {
    const char *src = NULL, *bootup = NULL, *dst = NULL;
    unsigned flags = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--src") && i + 1 < argc) src = argv[++i];
        else if (!strcmp(argv[i], "--bootup") && i + 1 < argc) bootup = argv[++i];
        else if (!strcmp(argv[i], "--dst") && i + 1 < argc) dst = argv[++i];
        else if (!strcmp(argv[i], "--allow-switch")) flags |= SC_ALLOW_SWITCH;
    }
    if (!src || !bootup || !dst) {
        fprintf(stderr, "usage: test_save_convert convert --src <folder> --bootup <Bootup.pack> --dst <new folder>\n");
        return 2;
    }
    ScReport r = {0};
    ScFormat *fmt = NULL;
    ScStage *stage = NULL;
    if (sc_format_load(bootup, &fmt, &r) != SC_OK || sc_convert(src, dst, fmt, flags, &stage, &r) != SC_OK) {
        printf("RESULT error %d %s\n", r.code, r.detail);
        sc_format_free(fmt);
        sc_report_free(&r);
        return 1;
    }
    printf("ROOT %s\n", r.root);
    for (int i = 0; i < r.nfiles; i++) {
        const ScFlagFile *f = &r.files[i];
        printf("FILE %s 0x%X 0x%X %u %u %u %u\n", f->file, f->src_rev, f->dst_rev, f->kept, f->defaulted, f->dropped, f->resized);
    }
    printf("COUNTS slots=%d tracker=%d album=%d pict=%d written=%d\n", r.nslots, r.n_tracker, r.n_album, r.n_pict, r.n_written);
    printf("RESULT ok\n");
    sc_format_free(fmt);
    sc_report_free(&r);
    free(stage);  // (the converted folder stays: the caller compares it)
    return 0;
}

// test_save_convert slots <save_0 folder>: one line per slot
static int slots_cmd(const char *dir) {
    ScSlot *v;
    int n = sc_list_slots(dir, &v);
    for (int i = 0; i < n; i++)
        printf("SLOT %s %lld %lld %d\n", v[i].name, (long long)v[i].saved, (long long)v[i].play_seconds, v[i].has_picture);
    free(v);
    return 0;
}

// ---------------------------------------------------------------- selftest
static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put(const char *dir, const char *rel, const char *text) {
    char p[1100], *s;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    for (s = p + 1; (s = strchr(s, '/')); *s++ = '/') {
        *s = 0;
        mkdir(p, 0755);
    }
    FILE *f = fopen(p, "wb");
    assert(f);
    fputs(text, f);
    fclose(f);
}

static char *slurp(const char *dir, const char *rel) {
    char p[1100];
    static char buf[256];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    buf[n] = 0;
    fclose(f);
    return buf;
}

static bool is_text(const char *dir, const char *rel, const char *want) {
    char *got = slurp(dir, rel);
    return got && !strcmp(got, want);
}

static bool has(const char *dir, const char *rel) {
    char p[1100];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    return stat(p, &st) == 0;
}

// An old save folder (save_0 with a slot, an SD card folder) and a staged one, in a scratch folder
static void setup(const char *root, bool with_old) {
    if (with_old) {
        put(root, "save/save_0/0/game_data.sav", "OLD");
        put(root, "save/save_0/tracker/t0", "OLDTRACK");
        put(root, "save/sdcard/note.txt", "SD");
    }
}

// A staged folder as sc_convert leaves it, but made by hand: <name>/save_0/0/game_data.sav = "NEW", with the list of
// what was made
static ScStage *fake_stage(const char *root, const char *name) {
    (void)root;
    put(name, "save_0/0/game_data.sav", "NEW");
    ScStage *s = calloc(1, sizeof *s);
    snprintf(s->dir, sizeof s->dir, "%s", name);
    static const char *const rel[] = {"", "/save_0", "/save_0/0", "/save_0/0/game_data.sav"};
    for (int i = 0; i < 4; i++) {
        char p[1100];
        snprintf(p, sizeof p, "%s%s", name, rel[i]);
        made_add(&s->made, p, i < 3);
    }
    return s;
}

static char *scratch(const char *base, char *out, size_t cap) {
    static int counter;
    snprintf(out, cap, "%s/t%d", base, ++counter);
    mkdir(base, 0755);
    assert(mkdir(out, 0755) == 0);
    return out;
}

static void selftest(const char *base) {
    char root[1100], root2[1100], root3[1100], root4[1100];
    scratch(base, root, sizeof root);
    char save[1100], backup[1100], stage[1100];
    snprintf(save, sizeof save, "%s/save", root);

    // 1. install over an existing save: the old folder is moved whole to the backup, the new one takes its place,
    // and what the import doesn't replace (the SD card folder) is still there
    setup(root, true);
    sc_backup_path(save, backup, sizeof backup);
    CHECK(strstr(backup, "/save-backups/save-") != NULL);
    sc_stage_path(save, stage, sizeof stage);
    CHECK(strstr(stage, "/save-import-") != NULL);
    ScStage *st = fake_stage(root, stage);
    ScReport r = {0};
    CHECK(sc_stage_install(st, save, backup, &r) == SC_OK);
    CHECK(is_text(save, "save_0/0/game_data.sav", "NEW"));
    CHECK(!has(save, "save_0/tracker/t0"));
    CHECK(is_text(save, "sdcard/note.txt", "SD"));
    CHECK(is_text(backup, "save_0/0/game_data.sav", "OLD"));
    CHECK(is_text(backup, "save_0/tracker/t0", "OLDTRACK"));
    CHECK(is_text(backup, "sdcard/note.txt", "SD"));
    CHECK(!has(stage, ""));
    sc_report_free(&r);

    // 2. the staged folder is gone when the swap happens: the second step fails and the first is undone
    scratch(base, root2, sizeof root2);
    snprintf(save, sizeof save, "%s/save", root2);
    setup(root2, true);
    sc_backup_path(save, backup, sizeof backup);
    sc_stage_path(save, stage, sizeof stage);
    st = fake_stage(root2, stage);
    char gone[1100];
    snprintf(gone, sizeof gone, "%s/gone", root2);
    rename(stage, gone);  // (the stage's folder is no longer there)
    memset(&r, 0, sizeof r);
    CHECK(sc_stage_install(st, save, backup, &r) == SC_IO);
    CHECK(is_text(save, "save_0/0/game_data.sav", "OLD"));
    CHECK(is_text(save, "sdcard/note.txt", "SD"));
    CHECK(!has(backup, ""));
    sc_stage_discard(st);
    sc_report_free(&r);

    // 3. no old save: nothing to back up, the staged folder just becomes the save folder
    scratch(base, root3, sizeof root3);
    snprintf(save, sizeof save, "%s/save", root3);
    sc_backup_path(save, backup, sizeof backup);
    sc_stage_path(save, stage, sizeof stage);
    st = fake_stage(root3, stage);
    memset(&r, 0, sizeof r);
    CHECK(sc_stage_install(st, save, backup, &r) == SC_OK);
    CHECK(is_text(save, "save_0/0/game_data.sav", "NEW"));
    CHECK(!has(backup, ""));
    sc_report_free(&r);

    // 4. a backup copy: the save stays, the copy is the same; a backup that can't be made leaves nothing
    memset(&r, 0, sizeof r);
    sc_backup_path(save, backup, sizeof backup);
    CHECK(sc_backup(save, backup, &r) == SC_OK && r.n_written == 1);
    CHECK(is_text(backup, "save_0/0/game_data.sav", "NEW") && is_text(save, "save_0/0/game_data.sav", "NEW"));
    CHECK(sc_backup(save, backup, &r) == SC_IO);  // (exists already: untouched)
    CHECK(is_text(backup, "save_0/0/game_data.sav", "NEW"));
    char nosave[1100], blocked[1100];
    snprintf(nosave, sizeof nosave, "%s/none", root3);
    CHECK(sc_backup(nosave, backup, &r) == SC_NO_SAVE);
    put(root3, "blocker", "x");  // a file where the backup's parent folder should be
    snprintf(blocked, sizeof blocked, "%s/blocker/inside", root3);
    CHECK(sc_backup(save, blocked, &r) == SC_IO);
    CHECK(!has(root3, "blocker/inside"));
    sc_report_free(&r);

    // 5. a backup that fails halfway removes what it copied, and only that
    {
        scratch(base, root4, sizeof root4);
        char s4[1100], b4[1100], deep[1100];
        snprintf(s4, sizeof s4, "%s/save", root4);
        snprintf(b4, sizeof b4, "%s/b", root4);
        put(root4, "save/a/one", "1");
        put(root4, "save/b/two", "2");
        snprintf(deep, sizeof deep, "%s/save/b/two", root4);
        memset(&r, 0, sizeof r);
#ifndef _WIN32
        chmod(deep, 0);  // unreadable: the copy of it fails (not for root, which reads anything)
        ScCode c = sc_backup(s4, b4, &r);
        chmod(deep, 0644);
        if (geteuid() != 0) {
            CHECK(c == SC_IO);
            CHECK(!has(b4, ""));
        }
#else
        (void)deep;
#endif
        CHECK(is_text(s4, "a/one", "1"));
        sc_report_free(&r);
    }

    // 6. discarding a stage removes what the conversion made, not what somebody put there since
    st = fake_stage(root3, stage);
    put(stage, "save_0/0/mine.txt", "MINE");
    sc_stage_discard(st);
    CHECK(!has(stage, "save_0/0/game_data.sav"));
    CHECK(is_text(stage, "save_0/0/mine.txt", "MINE"));

    // 7. the profile folder is found below the one picked
    char prof[1100];
    put(root3, "cemu/mlc01/usr/save/00050000/101c9500/user/80000001/0/game_data.sav", "x");
    put(root3, "cemu/mlc01/usr/save/00050000/101c9500/user/common/readme", "x");
    snprintf(nosave, sizeof nosave, "%s/cemu", root3);
    CHECK(sc_find_profile(nosave, prof, sizeof prof) == SC_OK && strstr(prof, "/user/80000001") != NULL);
    snprintf(nosave, sizeof nosave, "%s/cemu/mlc01/usr/save/00050000/101c9500/user/common", root3);
    CHECK(sc_find_profile(nosave, prof, sizeof prof) == SC_NO_SAVE);

    // 8. slots
    put(root3, "ls/2/game_data.sav", "x");
    put(root3, "ls/10/caption.sav", "x");
    put(root3, "ls/10/caption.jpg", "x");
    put(root3, "ls/1/other.txt", "x");  // (not a slot)
    put(root3, "ls/word/game_data.sav", "x");
    snprintf(nosave, sizeof nosave, "%s/ls", root3);
    ScSlot *slots;
    int n = sc_list_slots(nosave, &slots);
    CHECK(n == 2 && slots[0].number == 2 && slots[1].number == 10 && slots[1].has_picture && !slots[0].has_picture);
    CHECK(slots[1].saved > 0 && slots[1].play_seconds == -1);
    free(slots);
    snprintf(nosave, sizeof nosave, "%s/not-there", root3);
    CHECK(sc_list_slots(nosave, &slots) == 0);
    free(slots);
    printf(failures ? "SELFTEST FAIL (%d)\n" : "SELFTEST PASS\n", failures);
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "convert")) return convert_cmd(argc, argv);
    if (argc >= 3 && !strcmp(argv[1], "slots")) return slots_cmd(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "selftest")) {
        selftest(argv[2]);
        return failures != 0;
    }
    fprintf(stderr, "usage: test_save_convert convert ... | slots <folder> | selftest <scratch folder>\n");
    return 2;
}
