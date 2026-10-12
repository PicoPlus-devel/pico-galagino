/*
 * The ROM sets of the games: the files GalaginoPlus's romconv scripts read,
 * identified by MAME's CRC32 and size. The first six are Galagino's (roms/
 * README.md there); the tables of the others were generated from GalaginoPlus's
 * scripts and checked against MAME's merged sets, which pico-bootLoader's
 * updateAll downloads. Their region split follows the roles in those scripts
 * and is refined when a game is ported. The names are
 * for messages only; the loader matches by CRC, so a set is found whatever the
 * files are called and wherever they sit in a zip (Pac-Man's PROMs are
 * pm1-*.* in MAME's puckman parent set, for instance).
 *
 * Each file goes to a region of the game at an offset. Regions in a game's
 * temp mask are only needed while its data is converted: Pac-Man keeps its
 * program ROM, Galaga its three, Donkey Kong both CPUs' ROMs, Frogger both,
 * Dig Dug its three and the playfield map, 1942 its banked program and sound
 * ROMs.
 */
#include "gal_internal.h"

/* MAME pacman (Midway), a clone of puckman. */
static const gal_romfile_t pacman_files[] = {
    {"pacman.6e", 0xc1e6ab10, 0x1000, 0, 0x0000},
    {"pacman.6f", 0x1a6fb2d4, 0x1000, 0, 0x1000},
    {"pacman.6h", 0xbcdd1beb, 0x1000, 0, 0x2000},
    {"pacman.6j", 0x817d94e3, 0x1000, 0, 0x3000},
    {"pacman.5e", 0x0c944964, 0x1000, 1, 0x0000}, /* characters */
    {"pacman.5f", 0x958fedf9, 0x1000, 2, 0x0000}, /* sprites */
    {"82s123.7f", 0x2fc650bd, 0x0020, 3, 0x0000}, /* palette */
    {"82s126.4a", 0x3eb3a8e4, 0x0100, 4, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 5, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 5, 0x0100},
};

/* MAME galaga (Namco rev. B). */
static const gal_romfile_t galaga_files[] = {
    {"gg1_1b.3p", 0xab036c9f, 0x1000, 0, 0x0000},
    {"gg1_2b.3m", 0xd9232240, 0x1000, 0, 0x1000},
    {"gg1_3.2m", 0x753ce503, 0x1000, 0, 0x2000},
    {"gg1_4b.2l", 0x499fcc76, 0x1000, 0, 0x3000},
    {"gg1_5b.3f", 0xbb5caae3, 0x1000, 1, 0x0000},
    {"gg1_7b.2c", 0xd016686b, 0x1000, 2, 0x0000},
    {"gg1_9.4l", 0x58b2f47c, 0x1000, 3, 0x0000},  /* characters */
    {"gg1_11.4d", 0xad447c80, 0x1000, 4, 0x0000}, /* sprites */
    {"gg1_10.4f", 0xdd6f1afc, 0x1000, 4, 0x1000},
    {"prom-5.5n", 0x54603c6b, 0x0020, 5, 0x0000}, /* palette */
    {"prom-4.2n", 0x59b6edab, 0x0100, 6, 0x0000}, /* character colour lookup */
    {"prom-3.1c", 0x4a04bb6b, 0x0100, 7, 0x0000}, /* sprite colour lookup */
    {"prom-1.1d", 0x7a2815b4, 0x0100, 8, 0x0000}, /* sound waveforms */
};

/* MAME dkong (US set 1). */
static const gal_romfile_t dkong_files[] = {
    {"c_5et_g.bin", 0xba70b88b, 0x1000, 0, 0x0000},
    {"c_5ct_g.bin", 0x5ec461ec, 0x1000, 0, 0x1000},
    {"c_5bt_g.bin", 0x1c97d324, 0x1000, 0, 0x2000},
    {"c_5at_g.bin", 0xb9005ac0, 0x1000, 0, 0x3000},
    {"s_3i_b.bin", 0x45a4ed06, 0x0800, 1, 0x0000}, /* sound CPU */
    {"s_3j_b.bin", 0x4743fe92, 0x0800, 1, 0x0800},
    {"v_5h_b.bin", 0x12c8c95d, 0x0800, 2, 0x0000}, /* characters */
    {"v_3pt.bin", 0x15e9c5e9, 0x0800, 2, 0x0800},
    {"l_4m_b.bin", 0x59f8054d, 0x0800, 3, 0x0000}, /* sprites */
    {"l_4n_b.bin", 0x672e4714, 0x0800, 3, 0x0800},
    {"l_4r_b.bin", 0xfeaa59ee, 0x0800, 3, 0x1000},
    {"l_4s_b.bin", 0x20f2ef7e, 0x0800, 3, 0x1800},
    {"c-2k.bpr", 0xe273ede5, 0x0100, 4, 0x0000}, /* palette, low nibble */
    {"c-2j.bpr", 0xd6412358, 0x0100, 5, 0x0000}, /* palette, high nibble */
    {"v-5e.bpr", 0xb869b8f5, 0x0100, 6, 0x0000}, /* character colour lookup */
};

/* MAME frogger. */
static const gal_romfile_t frogger_files[] = {
    {"frogger.26", 0x597696d6, 0x1000, 0, 0x0000},
    {"frogger.27", 0xb6e6fcc3, 0x1000, 0, 0x1000},
    {"frsm3.7", 0xaca22ae0, 0x1000, 0, 0x2000},
    {"frogger.608", 0xe8ab0256, 0x0800, 1, 0x0000}, /* sound CPU */
    {"frogger.609", 0x7380a48f, 0x0800, 1, 0x0800},
    {"frogger.610", 0x31d7eb27, 0x0800, 1, 0x1000},
    {"frogger.606", 0xf524ee30, 0x0800, 2, 0x0000}, /* characters and sprites */
    {"frogger.607", 0x05f7d883, 0x0800, 2, 0x0800},
    {"pr-91.6l", 0x413703bf, 0x0020, 3, 0x0000},    /* palette */
};

/* MAME digdug (rev 2). */
static const gal_romfile_t digdug_files[] = {
    {"dd1a.1", 0xa80ec984, 0x1000, 0, 0x0000},
    {"dd1a.2", 0x559f00bd, 0x1000, 0, 0x1000},
    {"dd1a.3", 0x8cbc6fe1, 0x1000, 0, 0x2000},
    {"dd1a.4", 0xd066f830, 0x1000, 0, 0x3000},
    {"dd1a.5", 0x6687933b, 0x1000, 1, 0x0000},
    {"dd1a.6", 0x843d857f, 0x1000, 1, 0x1000},
    {"dd1.7", 0xa41bce72, 0x1000, 2, 0x0000},
    {"dd1.9", 0xf14a6fe1, 0x0800, 3, 0x0000},      /* characters */
    {"dd1.10b", 0x2cf399c2, 0x1000, 4, 0x0000},    /* playfield map */
    {"dd1.11", 0x7b383983, 0x1000, 5, 0x0000},     /* playfield tiles */
    {"dd1.15", 0xe22957c8, 0x1000, 6, 0x0000},     /* sprites */
    {"dd1.14", 0x2829ec99, 0x1000, 6, 0x1000},
    {"dd1.13", 0x458499e9, 0x1000, 6, 0x2000},
    {"dd1.12", 0xc58252a0, 0x1000, 6, 0x3000},
    {"136007.113", 0x4cb9da99, 0x0020, 7, 0x0000}, /* palette */
    {"136007.111", 0x00c7c419, 0x0100, 8, 0x0000}, /* sprite colour lookup */
    {"136007.112", 0xe9b3e08e, 0x0100, 9, 0x0000}, /* playfield colour lookup */
    {"136007.110", 0x7a2815b4, 0x0100, 10, 0x0000}, /* sound waveforms */
    {"136007.109", 0x77245b66, 0x0100, 10, 0x0100},
};

/* MAME 1942 (Revision B). */
static const gal_romfile_t g1942_files[] = {
    {"srb-03.m3", 0xd9dafcc3, 0x4000, 0, 0x0000},
    {"srb-04.m4", 0xda0cf924, 0x4000, 0, 0x4000},
    {"srb-05.m5", 0xd102911c, 0x4000, 1, 0x0000}, /* banked */
    {"srb-06.m6", 0x466f8248, 0x2000, 2, 0x0000}, /* 8 KB in a 16 KB bank, the rest reads 0 as in MAME */
    {"srb-07.m7", 0x0d31038c, 0x4000, 3, 0x0000},
    {"sr-01.c11", 0xbd87f06b, 0x4000, 4, 0x0000}, /* sound CPU */
    {"sr-02.f2", 0x6ebca191, 0x2000, 5, 0x0000},  /* characters */
    {"sr-08.a1", 0x3884d9eb, 0x2000, 6, 0x0000},  /* tiles */
    {"sr-09.a2", 0x999cf6e0, 0x2000, 6, 0x2000},
    {"sr-10.a3", 0x8edb273a, 0x2000, 6, 0x4000},
    {"sr-11.a4", 0x3a2726c3, 0x2000, 6, 0x6000},
    {"sr-12.a5", 0x1bd3d8bb, 0x2000, 6, 0x8000},
    {"sr-13.a6", 0x658f02c4, 0x2000, 6, 0xa000},
    {"sr-14.l1", 0x2528bec6, 0x4000, 7, 0x0000},  /* sprites */
    {"sr-15.l2", 0xf89287aa, 0x4000, 7, 0x4000},
    {"sr-16.n1", 0x024418f8, 0x4000, 7, 0x8000},
    {"sr-17.n2", 0xe2c7e489, 0x4000, 7, 0xc000},
    {"sb-5.e8", 0x93ab8153, 0x0100, 8, 0x0000},   /* palette red */
    {"sb-6.e9", 0x8ab44f7d, 0x0100, 8, 0x0100},   /* palette green */
    {"sb-7.e10", 0xf4ade9a4, 0x0100, 8, 0x0200},  /* palette blue */
    {"sb-0.f1", 0x6047d91b, 0x0100, 9, 0x0000},   /* character colour lookup */
    {"sb-4.d6", 0x4858968d, 0x0100, 10, 0x0000},  /* tile colour lookup */
    {"sb-3.d2", 0x3b0c99af, 0x0100, 10, 0x0100},
    {"sb-2.d1", 0x8bb8b3df, 0x0100, 10, 0x0200},
    {"sb-8.k3", 0xf6fad943, 0x0100, 11, 0x0000},  /* sprite colour lookup */
};

/* MAME alibaba. */
static const gal_romfile_t alibaba_files[] = {
    {"6e", 0x38d701aa, 0x1000, 0, 0x0000}, /* program */
    {"6f", 0x3d0e35f3, 0x1000, 0, 0x1000},
    {"6h", 0x823bee89, 0x1000, 0, 0x2000},
    {"6k", 0x474d032f, 0x1000, 0, 0x3000},
    {"6l", 0x5ab315c1, 0x1000, 0, 0x4000},
    {"6m", 0x438d0357, 0x0800, 0, 0x5000},
    {"ab7.bin", 0x52294ef5, 0x0800, 1, 0x0000}, /* mystery */
    {"5e", 0x85bcb8f8, 0x0800, 2, 0x0000}, /* characters */
    {"5h", 0x38e50862, 0x0800, 2, 0x0800},
    {"5f", 0xb5715c86, 0x0800, 3, 0x0000}, /* sprites */
    {"5k", 0x713086b3, 0x0800, 3, 0x0800},
    {"82s123.e7", 0x2fc650bd, 0x0020, 4, 0x0000}, /* palette */
    {"82s129.a4", 0x3eb3a8e4, 0x0100, 5, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 6, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 6, 0x0100},
};

/* MAME amidar1 (older), a clone of amidar. */
static const gal_romfile_t amidar_files[] = {
    {"amidar.2c", 0xc294bf27, 0x1000, 0, 0x0000}, /* program (amidar1) */
    {"amidar.2e", 0xe6e96826, 0x1000, 0, 0x1000},
    {"amidar.2f", 0x3656be6f, 0x1000, 0, 0x2000},
    {"amidar.2h", 0x1be170bd, 0x1000, 0, 0x3000},
    {"amidar.5c", 0xc4b66ae4, 0x1000, 1, 0x0000}, /* sound CPU */
    {"amidar.5d", 0x806785af, 0x1000, 1, 0x1000},
    {"amidar.5f", 0x5e51e84d, 0x0800, 2, 0x0000}, /* characters and sprites */
    {"amidar.5h", 0x2f7f1c30, 0x0800, 2, 0x0800},
    {"amidar.clr", 0xf940dcc3, 0x0020, 3, 0x0000}, /* palette */
};

/* MAME anteater. */
static const gal_romfile_t anteater_files[] = {
    {"ra1-2c", 0x58bc9393, 0x1000, 0, 0x0000}, /* program */
    {"ra1-2e", 0x574fc6f6, 0x1000, 0, 0x1000},
    {"ra1-2f", 0x2f7c1fe5, 0x1000, 0, 0x2000},
    {"ra1-2h", 0xae8a5da3, 0x1000, 0, 0x3000},
    {"ra4-5c", 0x87300b4f, 0x0800, 1, 0x0000}, /* sound CPU */
    {"ra4-5d", 0xaf4e5ffe, 0x0800, 1, 0x0800},
    {"ra6-5f", 0x4c3f8a08, 0x0800, 2, 0x0000}, /* characters and sprites */
    {"ra6-5h", 0xb30c7c9f, 0x0800, 2, 0x0800},
    {"colr6f.cpu", 0xfce333c7, 0x0020, 3, 0x0000}, /* palette */
};

/* MAME bagmanm2, a clone of bagman. */
static const gal_romfile_t bagman_files[] = {
    {"bagmanm2.1", 0x53769ebe, 0x2000, 0, 0x0000}, /* program (bagmanm2) */
    {"bagmanm2.2", 0x9435bb87, 0x2000, 0, 0x2000},
    {"bagmanm2.3", 0xf37ba7f6, 0x2000, 0, 0x4000},
    {"bagmanm2.9", 0xf1e70d9e, 0x2000, 1, 0x0000}, /* characters and sprites */
    {"bagmanm2.7", 0x777e48c4, 0x2000, 1, 0x2000},
    {"bagmanmc.clr", 0x6a0c7d87, 0x0020, 2, 0x0000}, /* palette */
};

/* MAME bombjack. */
static const gal_romfile_t bombjack_files[] = {
    {"09_j01b.bin", 0xc668dc30, 0x2000, 0, 0x0000}, /* program */
    {"10_l01b.bin", 0x52a1e5fb, 0x2000, 0, 0x2000},
    {"11_m01b.bin", 0xb68a062a, 0x2000, 0, 0x4000},
    {"12_n01b.bin", 0x1d3ecee5, 0x2000, 0, 0x6000},
    {"13.1r", 0x70e0244d, 0x2000, 0, 0x8000},
    {"01_h03t.bin", 0x8407917d, 0x2000, 1, 0x0000}, /* sound CPU */
    {"03_e08t.bin", 0x9f0470d5, 0x1000, 2, 0x0000}, /* foreground characters */
    {"04_h08t.bin", 0x81ec12e6, 0x1000, 2, 0x1000},
    {"05_k08t.bin", 0xe87ec8b1, 0x1000, 2, 0x2000},
    {"06_l08t.bin", 0x51eebd89, 0x2000, 3, 0x0000}, /* background tiles */
    {"07_n08t.bin", 0x9dd98e9d, 0x2000, 3, 0x2000},
    {"08_r08t.bin", 0x3155ee7d, 0x2000, 3, 0x4000},
    {"02_p04t.bin", 0x398d4a02, 0x1000, 4, 0x0000}, /* background maps */
    {"16_m07b.bin", 0x94694097, 0x2000, 5, 0x0000}, /* sprites */
    {"15_l07b.bin", 0x013f58f2, 0x2000, 5, 0x2000},
    {"14_j07b.bin", 0x101c858d, 0x2000, 5, 0x4000},
};

/* MAME bnjm (Bump 'n' Jump, Midway), a clone of brubber. */
static const gal_romfile_t bnj_files[] = {
    {"bnj12b.bin", 0xba3e3801, 0x2000, 0, 0x0000}, /* program (bnjm, encrypted) */
    {"bnj12c.bin", 0xfb3a2cdd, 0x2000, 0, 0x2000},
    {"bnj12d.bin", 0xb88bc99e, 0x2000, 0, 0x4000},
    {"bnj6c.bin", 0x8c02f662, 0x1000, 1, 0x0000}, /* sound CPU */
    {"bnj4e.bin", 0xb864d082, 0x2000, 2, 0x0000}, /* characters and sprites */
    {"bnj4f.bin", 0x6c31d77a, 0x2000, 2, 0x2000},
    {"bnj4h.bin", 0x5824e6fb, 0x2000, 2, 0x4000},
    {"bnj10e.bin", 0xf4e9eb49, 0x1000, 3, 0x0000}, /* background */
    {"bnj10f.bin", 0xa9ffacb4, 0x1000, 3, 0x1000},
};

/* MAME btime. */
static const gal_romfile_t btime_files[] = {
    {"aa04.9b", 0x368a25b5, 0x1000, 0, 0x0000}, /* program (encrypted) */
    {"aa06.13b", 0xb4ba400d, 0x1000, 0, 0x1000},
    {"aa05.10b", 0x8005bffa, 0x1000, 0, 0x2000},
    {"aa07.15b", 0x086440ad, 0x1000, 0, 0x3000},
    {"ab14.12h", 0xf55e5211, 0x1000, 1, 0x0000}, /* sound CPU */
    {"aa12.7k", 0xc4617243, 0x1000, 2, 0x0000}, /* characters and sprites */
    {"ab13.9k", 0xac01042f, 0x1000, 2, 0x1000},
    {"ab10.10k", 0x854a872a, 0x1000, 2, 0x2000},
    {"ab11.12k", 0xd4848014, 0x1000, 2, 0x3000},
    {"aa8.13k", 0x8650c788, 0x1000, 2, 0x4000},
    {"ab9.15k", 0x8dec15e6, 0x1000, 2, 0x5000},
    {"ab00.1b", 0xc7a14485, 0x0800, 3, 0x0000}, /* background tiles */
    {"ab01.3b", 0x25b49078, 0x0800, 3, 0x0800},
    {"ab02.4b", 0xb8ef56c3, 0x0800, 3, 0x1000},
    {"ab03.6b", 0xd26bc1f3, 0x0800, 4, 0x0000}, /* background map */
};

/* MAME circusc. */
static const gal_romfile_t circusc_files[] = {
    {"380_s05.3h", 0x48feafcf, 0x2000, 0, 0x0000}, /* program (KONAMI-1) */
    {"380_q04.4h", 0xc283b887, 0x2000, 0, 0x2000},
    {"380_q03.5h", 0xe90c0e86, 0x2000, 0, 0x4000},
    {"380_q02.6h", 0x4d847dc6, 0x2000, 0, 0x6000},
    {"380_q01.7h", 0x18c20adf, 0x2000, 0, 0x8000},
    {"380_l14.5c", 0x607df0fb, 0x2000, 1, 0x0000}, /* sound CPU */
    {"380_l15.7c", 0xa6ad30e1, 0x2000, 1, 0x2000},
    {"380_j12.4a", 0x56e5b408, 0x2000, 2, 0x0000}, /* characters */
    {"380_j13.5a", 0x5aca0193, 0x2000, 2, 0x2000},
    {"380_j06.11e", 0xdf0405c6, 0x2000, 3, 0x0000}, /* sprites */
    {"380_j07.12e", 0x23dfe3a6, 0x2000, 3, 0x2000},
    {"380_j08.13e", 0x3ba95390, 0x2000, 3, 0x4000},
    {"380_j09.14e", 0xa9fba85a, 0x2000, 3, 0x6000},
    {"380_j10.15e", 0x0532347e, 0x2000, 3, 0x8000},
    {"380_j11.16e", 0xe1725d24, 0x2000, 3, 0xa000},
    {"380_j18.2a", 0x10dd4eaa, 0x0020, 4, 0x0000}, /* palette */
    {"380_j17.7b", 0x13989357, 0x0100, 5, 0x0000}, /* character lookup */
    {"380_j16.10c", 0xc244f2aa, 0x0100, 6, 0x0000}, /* sprite lookup */
};

/* MAME crush. */
static const gal_romfile_t crush_files[] = {
    {"crushkrl.6e", 0xa8dd8f54, 0x1000, 0, 0x0000}, /* program */
    {"crushkrl.6f", 0x91387299, 0x1000, 0, 0x1000},
    {"crushkrl.6h", 0xd4455f27, 0x1000, 0, 0x2000},
    {"crushkrl.6j", 0xd59fc251, 0x1000, 0, 0x3000},
    {"maketrax.5e", 0x91bad2da, 0x1000, 1, 0x0000}, /* characters */
    {"maketrax.5f", 0xaea79f55, 0x1000, 2, 0x0000}, /* sprites */
    {"82s123.7f", 0x2fc650bd, 0x0020, 3, 0x0000}, /* palette */
    {"2s140.4a", 0x63efb927, 0x0100, 4, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 5, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 5, 0x0100},
};

/* MAME dkong3. */
static const gal_romfile_t dkong3_files[] = {
    {"dk3c.7b", 0x38d5f38e, 0x2000, 0, 0x0000}, /* program */
    {"dk3c.7c", 0xc9134379, 0x2000, 0, 0x2000},
    {"dk3c.7d", 0xd22e2921, 0x2000, 0, 0x4000},
    {"dk3c.7e", 0x615f14b7, 0x2000, 0, 0x6000},
    {"dk3c.5l", 0x7ff88885, 0x2000, 1, 0x0000}, /* sound CPU 1 */
    {"dk3c.6h", 0x36d7200c, 0x2000, 2, 0x0000}, /* sound CPU 2 */
    {"dk3v.3n", 0x415a99c7, 0x1000, 3, 0x0000}, /* characters */
    {"dk3v.3p", 0x25744ea0, 0x1000, 3, 0x1000},
    {"dk3v.7c", 0x8ffa1737, 0x1000, 4, 0x0000}, /* sprites */
    {"dk3v.7d", 0x9ac84686, 0x1000, 4, 0x1000},
    {"dk3v.7e", 0x0c0af3fb, 0x1000, 4, 0x2000},
    {"dk3v.7f", 0x55c58662, 0x1000, 4, 0x3000},
    {"dkc1-c.1d", 0xdf54befc, 0x0200, 5, 0x0000}, /* palette */
    {"dkc1-c.1c", 0x66a77f40, 0x0200, 5, 0x0200},
    {"dkc1-v.2n", 0x50e33434, 0x0100, 6, 0x0000}, /* character colours */
};

/* MAME dkongjrj (Japan), a clone of dkongjr. */
static const gal_romfile_t dkongjr_files[] = {
    {"c_5ba.bin", 0x50a015ce, 0x2000, 0, 0x0000}, /* program (dkongjrj) */
    {"c_5ca.bin", 0xc0a18f0d, 0x2000, 0, 0x2000},
    {"c_5ea.bin", 0xa81dd00c, 0x2000, 0, 0x4000},
    {"c_3h.bin", 0x715da5f8, 0x1000, 1, 0x0000}, /* sound CPU */
    {"v_3na.bin", 0xa95c4c63, 0x1000, 2, 0x0000}, /* characters */
    {"v_3pa.bin", 0x4974ffef, 0x1000, 2, 0x1000},
    {"v_7c.bin", 0xdc7f4164, 0x0800, 3, 0x0000}, /* sprites */
    {"v_7d.bin", 0x0ce7dcf6, 0x0800, 3, 0x0800},
    {"v_7e.bin", 0x24d1ff17, 0x0800, 3, 0x1000},
    {"v_7f.bin", 0x0f8c083f, 0x0800, 3, 0x1800},
    {"c-2e.bpr", 0x463dc7ad, 0x0100, 4, 0x0000}, /* palette */
    {"c-2f.bpr", 0x47ba0042, 0x0100, 4, 0x0100},
    {"v-2n.bpr", 0xdbf185bf, 0x0100, 5, 0x0000}, /* character colours */
};

/* MAME eyes. */
static const gal_romfile_t eyes_files[] = {
    {"d7", 0x3b09ac89, 0x1000, 0, 0x0000}, /* program (bit-swapped) */
    {"e7", 0x97096855, 0x1000, 0, 0x1000},
    {"f7", 0x731e294e, 0x1000, 0, 0x2000},
    {"h7", 0x22f7a719, 0x1000, 0, 0x3000},
    {"d5", 0xd6af0030, 0x1000, 1, 0x0000}, /* characters */
    {"e5", 0xa42b5201, 0x1000, 2, 0x0000}, /* sprites */
    {"82s123.7f", 0x2fc650bd, 0x0020, 3, 0x0000}, /* palette */
    {"82s129.4a", 0xd8d78829, 0x0100, 4, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 5, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 5, 0x0100},
};

/* MAME fantasyu (US). */
static const gal_romfile_t fantasy_files[] = {
    {"ic12.cpu", 0x22cb2249, 0x1000, 0, 0x0000}, /* program */
    {"ic07.cpu", 0x0e2880b6, 0x1000, 0, 0x1000},
    {"ic08.cpu", 0x4c331317, 0x1000, 0, 0x2000},
    {"ic09.cpu", 0x6ac1dbfc, 0x1000, 0, 0x3000},
    {"ic10.cpu", 0xc796a406, 0x1000, 0, 0x4000},
    {"ic14.cpu", 0x6f1f0698, 0x1000, 0, 0x5000},
    {"ic15.cpu", 0x5534d57e, 0x1000, 0, 0x6000},
    {"ic16.cpu", 0x6c2aeb6e, 0x1000, 0, 0x7000},
    {"ic17.cpu", 0xf6aa5de1, 0x1000, 0, 0x8000},
    {"fs10ic50.bin", 0x86a801c3, 0x1000, 1, 0x0000}, /* graphics */
    {"fs11ic51.bin", 0x9dfff71c, 0x1000, 1, 0x1000},
    {"fantasy.ic7", 0x361a5e99, 0x0020, 2, 0x0000}, /* palette */
    {"fantasy.ic6", 0x33d974f7, 0x0020, 2, 0x0020},
    {"fs_b_51.bin", 0x48094ec5, 0x0800, 3, 0x0000}, /* music */
    {"fs_a_52.bin", 0x1d0316e8, 0x0800, 3, 0x0800},
    {"fs_c_53.bin", 0x49fd4ae8, 0x0800, 3, 0x1000},
};

/* MAME galaxian. */
static const gal_romfile_t galaxian_files[] = {
    {"galmidw.u", 0x745e2d61, 0x0800, 0, 0x0000}, /* program */
    {"galmidw.v", 0x9c999a40, 0x0800, 0, 0x0800},
    {"galmidw.w", 0xb5894925, 0x0800, 0, 0x1000},
    {"galmidw.y", 0x6b3ca10b, 0x0800, 0, 0x1800},
    {"7l", 0x1b933207, 0x0800, 0, 0x2000},
    {"1h.bin", 0x39fb43a4, 0x0800, 1, 0x0000}, /* characters and sprites */
    {"1k.bin", 0x7e3f56a2, 0x0800, 1, 0x0800},
    {"6l.bpr", 0xc3ac9467, 0x0020, 2, 0x0000}, /* palette */
};

/* MAME gaplus, with the program of its clone galaga3. */
static const gal_romfile_t gaplus_files[] = {
    {"gp3-4c.8d", 0x10d7f64c, 0x2000, 0, 0x0000}, /* main CPU (galaga3) */
    {"gp3-3c.8c", 0x962411e8, 0x2000, 0, 0x2000},
    {"gp3-2d.8b", 0xecc01bdb, 0x2000, 0, 0x4000},
    {"gp3-8b.11d", 0xf5e056d1, 0x2000, 1, 0x0000}, /* sub CPU (galaga3) */
    {"gp2-7.11c", 0x0621f7df, 0x2000, 1, 0x2000},
    {"gp3-6b.11b", 0x026491b6, 0x2000, 1, 0x4000},
    {"gp2-1.4b", 0xed8aa206, 0x2000, 2, 0x0000}, /* sound CPU */
    {"gp2-5.8s", 0xf3d19987, 0x2000, 3, 0x0000}, /* characters */
    {"gp2-11.11p", 0x57740ff9, 0x2000, 4, 0x0000}, /* sprites */
    {"gp2-10.11n", 0x6cd8ce11, 0x2000, 4, 0x2000},
    {"gp2-12.11r", 0x7316a1f1, 0x2000, 4, 0x4000},
    {"gp2-9.11m", 0xe6a9ae67, 0x2000, 4, 0x6000},
    {"gp2-3.1p", 0xa5091352, 0x0100, 5, 0x0000}, /* palette */
    {"gp2-1.1n", 0x8bc8022a, 0x0100, 5, 0x0100},
    {"gp2-2.2n", 0x8dabc20b, 0x0100, 5, 0x0200},
    {"gp2-7.6s", 0x2faa3e09, 0x0100, 6, 0x0000}, /* character lookup */
    {"gp2-6.6p", 0x6f99c2da, 0x0200, 7, 0x0000}, /* sprite lookup */
    {"gp2-5.6n", 0xc7d31657, 0x0200, 7, 0x0200},
    {"gp2-4.3f", 0x2d9fbdd8, 0x0100, 8, 0x0000}, /* sound waveforms */
};

/* MAME gyruss. */
static const gal_romfile_t gyruss_files[] = {
    {"gyrussk.1", 0xc673b43d, 0x2000, 0, 0x0000}, /* main CPU */
    {"gyrussk.2", 0xa4ec03e4, 0x2000, 0, 0x2000},
    {"gyrussk.3", 0x27454a98, 0x2000, 0, 0x4000},
    {"gyrussk.9", 0x822bf27e, 0x2000, 1, 0x0000}, /* sub CPU (KONAMI-1) */
    {"gyrussk.1a", 0xf4ae1c17, 0x2000, 2, 0x0000}, /* sound CPU */
    {"gyrussk.2a", 0xba498115, 0x2000, 2, 0x2000},
    {"gyrussk.3a", 0x3f9b5dea, 0x1000, 3, 0x0000}, /* drums (i8039) */
    {"gyrussk.6", 0xc949db10, 0x2000, 4, 0x0000}, /* sprites */
    {"gyrussk.5", 0x4f22411a, 0x2000, 4, 0x2000},
    {"gyrussk.8", 0x47cd1fbc, 0x2000, 4, 0x4000},
    {"gyrussk.7", 0x8e8d388c, 0x2000, 4, 0x6000},
    {"gyrussk.4", 0x27d8329b, 0x2000, 5, 0x0000}, /* characters */
    {"gyrussk.pr3", 0x98782db3, 0x0020, 6, 0x0000}, /* palette */
    {"gyrussk.pr1", 0x7ed057de, 0x0100, 7, 0x0000}, /* sprite lookup */
    {"gyrussk.pr2", 0xde823a81, 0x0100, 8, 0x0000}, /* character lookup */
};

/* MAME ladybug. */
static const gal_romfile_t ladybug_files[] = {
    {"l1.c4", 0xd09e0adb, 0x1000, 0, 0x0000}, /* program */
    {"l2.d4", 0x88bc4a0a, 0x1000, 0, 0x1000},
    {"l3.e4", 0x53e9efce, 0x1000, 0, 0x2000},
    {"l4.h4", 0xffc424d7, 0x1000, 0, 0x3000},
    {"l5.j4", 0xad6af809, 0x1000, 0, 0x4000},
    {"l6.k4", 0xcf1acca4, 0x1000, 0, 0x5000},
    {"l9.f7", 0x77b1da1e, 0x1000, 1, 0x0000}, /* characters */
    {"l0.h7", 0xaa82e00b, 0x1000, 1, 0x1000},
    {"l8.l7", 0x8b99910b, 0x1000, 2, 0x0000}, /* sprites */
    {"l7.m7", 0x86a5b448, 0x1000, 2, 0x1000},
    {"10-2.k1", 0xdf091e52, 0x0020, 3, 0x0000}, /* palette */
    {"10-1.f4", 0x40640d8f, 0x0020, 4, 0x0000}, /* sprite lookup */
    {"10-3.c4", 0x27fa3a50, 0x0020, 4, 0x0020},
};

/* MAME lizwiz. */
static const gal_romfile_t lizwiz_files[] = {
    {"6e.cpu", 0x32bc1990, 0x1000, 0, 0x0000}, /* program */
    {"6f.cpu", 0xef24b414, 0x1000, 0, 0x1000},
    {"6h.cpu", 0x30bed83d, 0x1000, 0, 0x2000},
    {"6j.cpu", 0xdd09baeb, 0x1000, 0, 0x3000},
    {"wiza", 0xf6dea3a6, 0x1000, 0, 0x4000},
    {"wizb", 0xf27fb5a8, 0x1000, 0, 0x5000},
    {"5e.cpu", 0x45059e73, 0x1000, 1, 0x0000}, /* characters */
    {"5f.cpu", 0xd2469717, 0x1000, 2, 0x0000}, /* sprites */
    {"7f.cpu", 0x7549a947, 0x0020, 3, 0x0000}, /* palette */
    {"4a.cpu", 0x5fdca536, 0x0100, 4, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 5, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 5, 0x0100},
};

/* MAME mappy. */
static const gal_romfile_t mappy_files[] = {
    {"mpx_3.1d", 0x52e6c708, 0x2000, 0, 0x0000}, /* main CPU */
    {"mp1_2.1c", 0xa958a61c, 0x2000, 0, 0x2000},
    {"mpx_1.1b", 0x203766d4, 0x2000, 0, 0x4000},
    {"mp1_4.1k", 0x8182dd5b, 0x2000, 1, 0x0000}, /* sound CPU */
    {"mp1_5.3b", 0x16498b9f, 0x1000, 2, 0x0000}, /* characters */
    {"mp1_6.3m", 0xf2d9647a, 0x2000, 3, 0x0000}, /* sprites */
    {"mp1_7.3n", 0x757cf2b6, 0x2000, 3, 0x2000},
    {"mp1-5.5b", 0x56531268, 0x0020, 4, 0x0000}, /* palette */
    {"mp1-6.4c", 0x50765082, 0x0100, 5, 0x0000}, /* character lookup */
    {"mp1-7.5k", 0x5396bd78, 0x0100, 6, 0x0000}, /* sprite lookup */
    {"mp1-3.3m", 0x16a9166a, 0x0100, 7, 0x0000}, /* sound waveforms */
};

/* MAME mooncrst (Nichibutsu). */
static const gal_romfile_t mooncrst_files[] = {
    {"mc1", 0x7d954a7a, 0x0800, 0, 0x0000}, /* program (encrypted) */
    {"mc2", 0x44bb7cfa, 0x0800, 0, 0x0800},
    {"mc3", 0x9c412104, 0x0800, 0, 0x1000},
    {"mc4", 0x7e9b1ab5, 0x0800, 0, 0x1800},
    {"mc5.7r", 0x16c759af, 0x0800, 0, 0x2000},
    {"mc6.8d", 0x69bcafdb, 0x0800, 0, 0x2800},
    {"mc7.8e", 0xb50dbc46, 0x0800, 0, 0x3000},
    {"mc8", 0x18ca312b, 0x0800, 0, 0x3800},
    {"mcs_b", 0xfb0f1f81, 0x0800, 1, 0x0000}, /* characters and sprites */
    {"mcs_d", 0x13932a15, 0x0800, 1, 0x0800},
    {"mcs_a", 0x631ebb5a, 0x0800, 1, 0x1000},
    {"mcs_c", 0x24cfd145, 0x0800, 1, 0x1800},
    {"mmi6331.6l", 0x6a0c7d87, 0x0020, 2, 0x0000}, /* palette */
};

/* MAME mrdo. */
static const gal_romfile_t mrdo_files[] = {
    {"a4-01.bin", 0x03dcfba2, 0x2000, 0, 0x0000}, /* program */
    {"c4-02.bin", 0x0ecdd39c, 0x2000, 0, 0x2000},
    {"e4-03.bin", 0x358f5dc2, 0x2000, 0, 0x4000},
    {"f4-04.bin", 0xf4190cfc, 0x2000, 0, 0x6000},
    {"s8-09.bin", 0xaa80c5b6, 0x1000, 1, 0x0000}, /* foreground */
    {"u8-10.bin", 0xd20ec85b, 0x1000, 1, 0x1000},
    {"r8-08.bin", 0xdbdc9ffa, 0x1000, 2, 0x0000}, /* background */
    {"n8-07.bin", 0x4b9973db, 0x1000, 2, 0x1000},
    {"h5-05.bin", 0xe1218cc5, 0x1000, 3, 0x0000}, /* sprites */
    {"k5-06.bin", 0xb1f68b04, 0x1000, 3, 0x1000},
    {"u02--2.bin", 0x238a65d7, 0x0020, 4, 0x0000}, /* palette */
    {"t02--3.bin", 0xae263dc0, 0x0020, 4, 0x0020},
    {"f10--1.bin", 0x16ee4ca2, 0x0020, 5, 0x0000}, /* sprite lookup */
};

/* MAME mrtnt. */
static const gal_romfile_t mrtnt_files[] = {
    {"tnt.1", 0x0e836586, 0x1000, 0, 0x0000}, /* program (bit-swapped) */
    {"tnt.2", 0x779c4c5b, 0x1000, 0, 0x1000},
    {"tnt.3", 0xad6fc688, 0x1000, 0, 0x2000},
    {"tnt.4", 0xd77557b3, 0x1000, 0, 0x3000},
    {"tnt.5", 0x3038cc0e, 0x1000, 1, 0x0000}, /* characters */
    {"tnt.6", 0x97634d8b, 0x1000, 2, 0x0000}, /* sprites */
    {"82s123.7f", 0x2fc650bd, 0x0020, 3, 0x0000}, /* palette */
    {"82s126.4a", 0x3eb3a8e4, 0x0100, 4, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 5, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 5, 0x0100},
};

/* MAME mspacman: Pac-Man's program, and the Ms. Pac-Man ROMs u5-u7, which
 * are decrypted and patched over it (MAME's init_mspacman). */
static const gal_romfile_t mspacman_files[] = {
    {"pacman.6e", 0xc1e6ab10, 0x1000, 0, 0x0000}, /* Pac-Man program */
    {"pacman.6f", 0x1a6fb2d4, 0x1000, 0, 0x1000},
    {"pacman.6h", 0xbcdd1beb, 0x1000, 0, 0x2000},
    {"pacman.6j", 0x817d94e3, 0x1000, 0, 0x3000},
    {"u5", 0xf45fbbcd, 0x0800, 1, 0x0000}, /* Ms. Pac-Man, encrypted */
    {"u6", 0xa90e7000, 0x1000, 1, 0x0800},
    {"u7", 0xc82cd714, 0x1000, 1, 0x1800},
    {"5e", 0x5c281d01, 0x1000, 2, 0x0000}, /* characters */
    {"5f", 0x615af909, 0x1000, 3, 0x0000}, /* sprites */
    {"82s123.7f", 0x2fc650bd, 0x0020, 4, 0x0000}, /* palette */
    {"82s126.4a", 0x3eb3a8e4, 0x0100, 5, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 6, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 6, 0x0100},
};

/* MAME nibbler6 (rev. 6), a clone of nibbler. */
static const gal_romfile_t nibbler_files[] = {
    {"g960-52.12", 0xac6a802b, 0x1000, 0, 0x0000}, /* program (rev. 6) */
    {"g960-48.07", 0x35971364, 0x1000, 0, 0x1000},
    {"g960-49.08", 0x6b33b806, 0x1000, 0, 0x2000},
    {"g960-50.09", 0x91a4f98d, 0x1000, 0, 0x3000},
    {"g960-51.10", 0xa151d934, 0x1000, 0, 0x4000},
    {"g960-53.14", 0x063f05cc, 0x1000, 0, 0x5000},
    {"g960-54.15", 0x7205fb8d, 0x1000, 0, 0x6000},
    {"g960-55.16", 0x4bb39815, 0x1000, 0, 0x7000},
    {"g960-56.17", 0xed680f19, 0x1000, 0, 0x8000},
    {"g960-57.50", 0x01d4d0c2, 0x1000, 1, 0x0000}, /* graphics */
    {"g960-58.51", 0xfeff7faf, 0x1000, 1, 0x1000},
    {"g70805.ic7", 0xa5709ff3, 0x0020, 2, 0x0000}, /* palette */
    {"g70804.ic6", 0xdacd592d, 0x0020, 2, 0x0020},
    {"g959-43.51", 0x0345f8b7, 0x0800, 3, 0x0000}, /* music */
    {"g959-44.52", 0x87d67dee, 0x0800, 3, 0x0800},
    {"g959-45.53", 0x33189917, 0x0800, 3, 0x1000},
};

/* MAME pengoja (Japan, 315-5010 type, rev. C), the parent pengo of earlier
 * MAME versions. */
static const gal_romfile_t pengo_files[] = {
    {"ep1689c.8", 0xf37066a8, 0x1000, 0, 0x0000}, /* program (encrypted) */
    {"ep1690b.7", 0xbaf48143, 0x1000, 0, 0x1000},
    {"ep1691b.15", 0xadf0eba0, 0x1000, 0, 0x2000},
    {"ep1692b.14", 0xa086d60f, 0x1000, 0, 0x3000},
    {"ep1693b.21", 0xb72084ec, 0x1000, 0, 0x4000},
    {"ep1694b.20", 0x94194a89, 0x1000, 0, 0x5000},
    {"ep5118b.32", 0xaf7b12c4, 0x1000, 0, 0x6000},
    {"ep5119c.31", 0x933950fe, 0x1000, 0, 0x7000},
    {"ep1640.92", 0xd7eec6cd, 0x2000, 1, 0x0000}, /* characters and sprites */
    {"ep1695.105", 0x5bfd26e9, 0x2000, 1, 0x2000},
    {"pr1633.78", 0x3a5844ec, 0x0020, 2, 0x0000}, /* palette */
    {"pr1634.88", 0x766b139b, 0x0400, 3, 0x0000}, /* colour lookup */
    {"pr1635.51", 0xc29dea27, 0x0100, 4, 0x0000}, /* sound waveforms */
    {"pr1636.70", 0x77245b66, 0x0100, 4, 0x0100},
};

/* MAME phoenix (Amstar, set 1). */
static const gal_romfile_t phoenix_files[] = {
    {"ic45", 0x9f68086b, 0x0800, 0, 0x0000}, /* program */
    {"ic46", 0x273a4a82, 0x0800, 0, 0x0800},
    {"ic47", 0x3d4284b9, 0x0800, 0, 0x1000},
    {"ic48", 0xcb5d9915, 0x0800, 0, 0x1800},
    {"h5-ic49.5a", 0xa105e4e7, 0x0800, 0, 0x2000},
    {"h6-ic50.6a", 0xac5e9ec1, 0x0800, 0, 0x2800},
    {"h7-ic51.7a", 0x2eab35b4, 0x0800, 0, 0x3000},
    {"h8-ic52.8a", 0xaff8e9c5, 0x0800, 0, 0x3800},
    {"ic23.3d", 0x3c7e623f, 0x0800, 1, 0x0000}, /* background */
    {"ic24.4d", 0x59916d3b, 0x0800, 1, 0x0800},
    {"b1-ic39.3b", 0x53413e8f, 0x0800, 2, 0x0000}, /* foreground */
    {"b2-ic40.4b", 0x0be2ba91, 0x0800, 2, 0x0800},
    {"mmi6301.ic40", 0x79350b25, 0x0100, 3, 0x0000}, /* palette */
    {"mmi6301.ic41", 0xe176b768, 0x0100, 3, 0x0100},
};

/* MAME pbaction. */
static const gal_romfile_t pbaction_files[] = {
    {"b-p7.bin", 0x8d6dcaae, 0x4000, 0, 0x0000}, /* program */
    {"b-n7.bin", 0xd54d5402, 0x4000, 0, 0x4000},
    {"b-l7.bin", 0xe7412d68, 0x2000, 0, 0x8000},
    {"a-e3.bin", 0x0e53a91f, 0x2000, 1, 0x0000}, /* sound CPU */
    {"a-s6.bin", 0x9a74a8e1, 0x2000, 2, 0x0000}, /* foreground */
    {"a-s7.bin", 0x5ca6ad3c, 0x2000, 2, 0x2000},
    {"a-s8.bin", 0x9f00b757, 0x2000, 2, 0x4000},
    {"a-j5.bin", 0x21efe866, 0x4000, 3, 0x0000}, /* background */
    {"a-j6.bin", 0x7f984c80, 0x4000, 3, 0x4000},
    {"a-j7.bin", 0xdf69e51b, 0x4000, 3, 0x8000},
    {"a-j8.bin", 0x0094cb8b, 0x4000, 3, 0xc000},
    {"b-c7.bin", 0xd1795ef5, 0x2000, 4, 0x0000}, /* sprites */
    {"b-d7.bin", 0xf28df203, 0x2000, 4, 0x2000},
    {"b-f7.bin", 0xaf6e9817, 0x2000, 4, 0x4000},
};

/* MAME pooyan. */
static const gal_romfile_t pooyan_files[] = {
    {"1.4a", 0xbb319c63, 0x2000, 0, 0x0000}, /* program */
    {"2.5a", 0xa1463d98, 0x2000, 0, 0x2000},
    {"3.6a", 0xfe1a9e08, 0x2000, 0, 0x4000},
    {"4.7a", 0x9e0f9bcc, 0x2000, 0, 0x6000},
    {"xx.7a", 0xfbe2b368, 0x1000, 1, 0x0000}, /* sound CPU */
    {"xx.8a", 0xe1795b3d, 0x1000, 1, 0x1000},
    {"8.10g", 0x931b29eb, 0x1000, 2, 0x0000}, /* characters */
    {"7.9g", 0xbbe6d6e4, 0x1000, 2, 0x1000},
    {"6.9a", 0xb2d8c121, 0x1000, 3, 0x0000}, /* sprites */
    {"5.8a", 0x1097c2b6, 0x1000, 3, 0x1000},
    {"pooyan.pr1", 0xa06a6d0e, 0x0020, 4, 0x0000}, /* palette */
    {"pooyan.pr2", 0x82748c0b, 0x0100, 5, 0x0000}, /* sprite lookup */
    {"pooyan.pr3", 0x8cd4cd60, 0x0100, 6, 0x0000}, /* character lookup */
};

/* MAME rocnrope. */
static const gal_romfile_t rocnrope_files[] = {
    {"rr1.1h", 0x83093134, 0x2000, 0, 0x0000}, /* program (KONAMI-1) */
    {"rr2.2h", 0x75af8697, 0x2000, 0, 0x2000},
    {"rr3.3h", 0xb21372b1, 0x2000, 0, 0x4000},
    {"rr4.4h", 0x7acb2a05, 0x2000, 0, 0x6000},
    {"rnr_h5.vid", 0x150a6264, 0x2000, 0, 0x8000},
    {"rnr_7a.snd", 0x75d2c4e2, 0x1000, 1, 0x0000}, /* sound CPU */
    {"rnr_8a.snd", 0xca4325ae, 0x1000, 1, 0x1000},
    {"rnr_h12.vid", 0xe2114539, 0x2000, 2, 0x0000}, /* characters */
    {"rnr_h11.vid", 0x169a8f3f, 0x2000, 2, 0x2000},
    {"rnr_a11.vid", 0xafdaba5e, 0x2000, 3, 0x0000}, /* sprites */
    {"rnr_a12.vid", 0x054cafeb, 0x2000, 3, 0x2000},
    {"rnr_a9.vid", 0x9d2166b2, 0x2000, 3, 0x4000},
    {"rnr_a10.vid", 0xaff6e22f, 0x2000, 3, 0x6000},
    {"a17_prom.bin", 0x22ad2c3e, 0x0020, 4, 0x0000}, /* palette */
    {"b16_prom.bin", 0x750a9677, 0x0100, 5, 0x0000}, /* sprite lookup */
    {"rocnrope.pr3", 0xb5c75a27, 0x0100, 6, 0x0000}, /* character lookup */
};

/* MAME scramble. */
static const gal_romfile_t scramble_files[] = {
    {"s1.2d", 0xea35ccaa, 0x0800, 0, 0x0000}, /* program */
    {"s2.2e", 0xe7bba1b3, 0x0800, 0, 0x0800},
    {"s3.2f", 0x12d7fc3e, 0x0800, 0, 0x1000},
    {"s4.2h", 0xb59360eb, 0x0800, 0, 0x1800},
    {"s5.2j", 0x4919a91c, 0x0800, 0, 0x2000},
    {"s6.2l", 0x26a4547b, 0x0800, 0, 0x2800},
    {"s7.2m", 0x0bb49470, 0x0800, 0, 0x3000},
    {"s8.2p", 0x6a5740e5, 0x0800, 0, 0x3800},
    {"ot1.5c", 0xbcd297f0, 0x0800, 1, 0x0000}, /* sound CPU */
    {"ot2.5d", 0xde7912da, 0x0800, 1, 0x0800},
    {"ot3.5e", 0xba2fa933, 0x0800, 1, 0x1000},
    {"c2.5f", 0x4708845b, 0x0800, 2, 0x0000}, /* characters and sprites */
    {"c1.5h", 0x11fd2887, 0x0800, 2, 0x0800},
    {"c01s.6e", 0x4e3caeab, 0x0020, 3, 0x0000}, /* palette */
};

/* MAME invaders (Space Invaders, Taito). */
static const gal_romfile_t invaders_files[] = {
    {"invaders.h", 0x734f5ad8, 0x0800, 0, 0x0000}, /* program */
    {"invaders.g", 0x6bfaca4a, 0x0800, 0, 0x0800},
    {"invaders.f", 0x0ccead96, 0x0800, 0, 0x1000},
    {"invaders.e", 0x14e538b0, 0x0800, 0, 0x1800},
};

/* MAME starforc. */
static const gal_romfile_t starforc_files[] = {
    {"3.3p", 0x8ba27691, 0x4000, 0, 0x0000}, /* program */
    {"2.3mn", 0x0fc4d2d6, 0x4000, 0, 0x4000},
    {"1.3hj", 0x2735bb22, 0x2000, 1, 0x0000}, /* sound CPU */
    {"7.2fh", 0xf4803339, 0x1000, 2, 0x0000}, /* foreground */
    {"8.3fh", 0x96979684, 0x1000, 2, 0x1000},
    {"9.3fh", 0xeead1d5c, 0x1000, 2, 0x2000},
    {"15.10jk", 0xc3bda12f, 0x2000, 3, 0x0000}, /* background 1 */
    {"14.9jk", 0x9e9384fe, 0x2000, 3, 0x2000},
    {"13.8jk", 0x84603285, 0x2000, 3, 0x4000},
    {"12.10de", 0xfdd9e38b, 0x2000, 4, 0x0000}, /* background 2 */
    {"11.9de", 0x668aea14, 0x2000, 4, 0x2000},
    {"10.8de", 0xc62a19c1, 0x2000, 4, 0x4000},
    {"18.10pq", 0x6455c3ad, 0x1000, 5, 0x0000}, /* background 3 */
    {"17.9pq", 0x68c60d0f, 0x1000, 5, 0x1000},
    {"16.8pq", 0xce20b469, 0x1000, 5, 0x2000},
    {"6.10lm", 0x5468a21d, 0x4000, 6, 0x0000}, /* sprites */
    {"5.9lm", 0xf71717f8, 0x4000, 6, 0x4000},
    {"4.8lm", 0xdd9d68a4, 0x4000, 6, 0x8000},
    {"07b.bin", 0x68db8300, 0x0020, 7, 0x0000}, /* DAC waveform */
};

/* MAME scobra. */
static const gal_romfile_t scobra_files[] = {
    {"epr1265.2c", 0xa0744b3f, 0x1000, 0, 0x0000}, /* program */
    {"2e", 0x8e7245cd, 0x1000, 0, 0x1000},
    {"epr1267.2f", 0x47a4e6fb, 0x1000, 0, 0x2000},
    {"2h", 0x7244f21c, 0x1000, 0, 0x3000},
    {"epr1269.2j", 0xe1f8a801, 0x1000, 0, 0x4000},
    {"2l", 0xd52affde, 0x1000, 0, 0x5000},
    {"5c", 0xd4346959, 0x0800, 1, 0x0000}, /* sound CPU */
    {"5d", 0xcc025d95, 0x0800, 1, 0x0800},
    {"5e", 0x1628c53f, 0x0800, 1, 0x1000},
    {"epr1274.5h", 0x64d113b4, 0x0800, 2, 0x0000}, /* characters and sprites */
    {"epr1273.5f", 0xa96316d3, 0x0800, 2, 0x0800},
    {"82s123.6e", 0x9b87f90d, 0x0020, 3, 0x0000}, /* palette */
};

/* MAME theglobp (Pac-Man hardware), a clone of suprglob. */
static const gal_romfile_t theglob_files[] = {
    {"glob.u2", 0x829d0bea, 0x2000, 0, 0x0000}, /* program (EPOS) */
    {"glob.u3", 0x31de6628, 0x2000, 0, 0x2000},
    {"glob.5e", 0x53688260, 0x1000, 1, 0x0000}, /* characters */
    {"glob.5f", 0x051f59c7, 0x1000, 2, 0x0000}, /* sprites */
    {"glob.7f", 0x1f617527, 0x0020, 3, 0x0000}, /* palette */
    {"glob.4a", 0x28faa769, 0x0100, 4, 0x0000}, /* colour lookup */
    {"82s126.1m", 0xa9cc86bf, 0x0100, 5, 0x0000}, /* sound waveforms */
    {"82s126.3m", 0x77245b66, 0x0100, 5, 0x0100},
};

/* MAME timeplt. */
static const gal_romfile_t timeplt_files[] = {
    {"tm1", 0x1551f1b9, 0x2000, 0, 0x0000}, /* program */
    {"tm2", 0x58636cb5, 0x2000, 0, 0x2000},
    {"tm3", 0xff4e0d83, 0x2000, 0, 0x4000},
    {"tm7", 0xd66da813, 0x1000, 1, 0x0000}, /* sound CPU */
    {"tm6", 0xc2507f40, 0x2000, 2, 0x0000}, /* characters */
    {"tm4", 0x7e437c3e, 0x2000, 3, 0x0000}, /* sprites */
    {"tm5", 0xe8ca87b9, 0x2000, 3, 0x2000},
    {"timeplt.b4", 0x34c91839, 0x0020, 4, 0x0000}, /* palette */
    {"timeplt.b5", 0x463b2b07, 0x0020, 4, 0x0020},
    {"timeplt.e9", 0x4bbb2150, 0x0100, 5, 0x0000}, /* sprite lookup */
    {"timeplt.e12", 0xf7b7663e, 0x0100, 6, 0x0000}, /* character lookup */
};

/* MAME todruaga. */
static const gal_romfile_t todruaga_files[] = {
    {"td2_3.1d", 0xfbf16299, 0x4000, 0, 0x0000}, /* main CPU */
    {"td2_1.1b", 0xb238d723, 0x4000, 0, 0x4000},
    {"td1_4.1k", 0xae9d06d9, 0x2000, 1, 0x0000}, /* sound CPU */
    {"td1_5.3b", 0xd32b249f, 0x1000, 2, 0x0000}, /* characters */
    {"td1_6.3m", 0xe827e787, 0x2000, 3, 0x0000}, /* sprites */
    {"td1_7.3n", 0x962bd060, 0x2000, 3, 0x2000},
    {"td1-5.5b", 0x122cc395, 0x0020, 4, 0x0000}, /* palette */
    {"td1-6.4c", 0x8c661d6a, 0x0100, 5, 0x0000}, /* character lookup */
    {"td1-7.5k", 0xa86c74dd, 0x0400, 6, 0x0000}, /* sprite lookup */
    {"td1-3.3m", 0x07104c40, 0x0100, 7, 0x0000}, /* sound waveforms */
};

/* MAME turtles. */
static const gal_romfile_t turtles_files[] = {
    {"turt_vid.2c", 0xec5e61fb, 0x1000, 0, 0x0000}, /* program */
    {"turt_vid.2e", 0xfd10821e, 0x1000, 0, 0x1000},
    {"turt_vid.2f", 0xddcfc5fa, 0x1000, 0, 0x2000},
    {"turt_vid.2h", 0x9e71696c, 0x1000, 0, 0x3000},
    {"turt_vid.2j", 0xfcd49fef, 0x1000, 0, 0x4000},
    {"turt_snd.5c", 0xf0c30f9a, 0x1000, 1, 0x0000}, /* sound CPU */
    {"turt_snd.5d", 0xaf5fc43c, 0x1000, 1, 0x1000},
    {"turt_vid.5h", 0xe5999d52, 0x0800, 2, 0x0000}, /* characters and sprites */
    {"turt_vid.5f", 0xc3ffd655, 0x0800, 2, 0x0800},
    {"turtles.clr", 0xf3ef02dd, 0x0020, 3, 0x0000}, /* palette */
};

/* MAME tutankhm. */
static const gal_romfile_t tutankhm_files[] = {
    {"m1.1h", 0xda18679f, 0x1000, 0, 0x0000}, /* program */
    {"m2.2h", 0xa0f02c85, 0x1000, 0, 0x1000},
    {"3j.3h", 0xea03a1ab, 0x1000, 0, 0x2000},
    {"m4.4h", 0xbd06fad0, 0x1000, 0, 0x3000},
    {"m5.5h", 0xbf9fd9b0, 0x1000, 0, 0x4000},
    {"j6.6h", 0xfe079c5b, 0x1000, 0, 0x5000},
    {"c1.1i", 0x7eb59b21, 0x1000, 1, 0x0000}, /* banked */
    {"c2.2i", 0x6615eff3, 0x1000, 1, 0x1000},
    {"c3.3i", 0xa10d4444, 0x1000, 1, 0x2000},
    {"c4.4i", 0x58cd143c, 0x1000, 1, 0x3000},
    {"c5.5i", 0xd7e7ae95, 0x1000, 1, 0x4000},
    {"c6.6i", 0x91f62b82, 0x1000, 1, 0x5000},
    {"c7.7i", 0xafd0a81f, 0x1000, 1, 0x6000},
    {"c8.8i", 0xdabb609b, 0x1000, 1, 0x7000},
    {"c9.9i", 0x8ea9c6a6, 0x1000, 1, 0x8000},
    {"s1.7a", 0xb52d01fa, 0x1000, 2, 0x0000}, /* sound CPU */
    {"s2.8a", 0x9db5c0ce, 0x1000, 2, 0x1000},
};

/* MAME vanvan. */
static const gal_romfile_t vanvan_files[] = {
    {"van-1.50", 0xcf1b2df0, 0x1000, 0, 0x0000}, /* program */
    {"van-2.51", 0xdf58e1cb, 0x1000, 0, 0x1000},
    {"van-3.52", 0x15571e24, 0x1000, 0, 0x2000},
    {"van-4.53", 0xb724cbe0, 0x1000, 0, 0x3000},
    {"van-5.39", 0xdb67414c, 0x1000, 0, 0x4000},
    {"van-20.18", 0x60efbe66, 0x1000, 1, 0x0000}, /* characters */
    {"van-21.19", 0x5dd53723, 0x1000, 2, 0x0000}, /* sprites */
    {"6331-1.6", 0xce1d9503, 0x0020, 3, 0x0000}, /* palette */
    {"6301-1.37", 0x4b803d9f, 0x0100, 4, 0x0000}, /* colour lookup */
};

/* MAME vanguard. */
static const gal_romfile_t vanguard_files[] = {
    {"sk4_ic07.bin", 0x6a29e354, 0x1000, 0, 0x0000}, /* program */
    {"sk4_ic08.bin", 0x302bba54, 0x1000, 0, 0x1000},
    {"sk4_ic09.bin", 0x424755f6, 0x1000, 0, 0x2000},
    {"sk4_ic10.bin", 0x54603274, 0x1000, 0, 0x3000},
    {"sk4_ic13.bin", 0xfde157d0, 0x1000, 0, 0x4000},
    {"sk4_ic14.bin", 0x0d5b47d0, 0x1000, 0, 0x5000},
    {"sk4_ic15.bin", 0x8549b8f8, 0x1000, 0, 0x6000},
    {"sk4_ic16.bin", 0x062e0be2, 0x1000, 0, 0x7000},
    {"sk5_ic50.bin", 0xe7d4315b, 0x0800, 1, 0x0000}, /* graphics */
    {"sk5_ic51.bin", 0x96e87858, 0x0800, 1, 0x0800},
    {"sk5_ic7.bin", 0xad782a73, 0x0020, 2, 0x0000}, /* palette */
    {"sk5_ic6.bin", 0x7dc9d450, 0x0020, 2, 0x0020},
    {"sk4_ic51.bin", 0xd2a64006, 0x0800, 3, 0x0000}, /* music */
    {"sk4_ic52.bin", 0xcc4a0b6f, 0x0800, 3, 0x0800},
};

/* MAME xevious. */
static const gal_romfile_t xevious_files[] = {
    {"xvi_1.3p", 0x09964dda, 0x1000, 0, 0x0000}, /* main CPU */
    {"xvi_2.3m", 0x60ecce84, 0x1000, 0, 0x1000},
    {"xvi_3.2m", 0x79754b7d, 0x1000, 0, 0x2000},
    {"xvi_4.2l", 0xc7d4bbf0, 0x1000, 0, 0x3000},
    {"xvi_5.3f", 0xc85b703f, 0x1000, 1, 0x0000}, /* sub CPU */
    {"xvi_6.3j", 0xe18cdaad, 0x1000, 1, 0x1000},
    {"xvi_7.2c", 0xdd35cf1c, 0x1000, 2, 0x0000}, /* sound CPU */
    {"xvi_12.3b", 0x088c8b26, 0x1000, 3, 0x0000}, /* foreground characters */
    {"xvi_13.3c", 0xde60ba25, 0x1000, 4, 0x0000}, /* background tiles */
    {"xvi_14.3d", 0x535cdbbc, 0x1000, 4, 0x1000},
    {"xvi_15.4m", 0xdc2c0ecb, 0x2000, 5, 0x0000}, /* sprites */
    {"xvi_17.4p", 0xdfb587ce, 0x2000, 5, 0x2000},
    {"xvi_16.4n", 0x605ca889, 0x1000, 5, 0x4000},
    {"xvi_18.4r", 0x02417d19, 0x2000, 5, 0x5000},
    {"xvi_9.2a", 0x57ed9879, 0x1000, 6, 0x0000}, /* planet map */
    {"xvi_10.2b", 0xae3ba9e5, 0x2000, 6, 0x1000},
    {"xvi_11.2c", 0x31e244dd, 0x1000, 6, 0x3000},
    {"xvi-8.6a", 0x5cc2727f, 0x0100, 7, 0x0000}, /* palette */
    {"xvi-9.6d", 0x5c8796cc, 0x0100, 7, 0x0100},
    {"xvi-10.6e", 0x3cb60975, 0x0100, 7, 0x0200},
    {"xvi-6.4f", 0x3a7599f0, 0x0200, 8, 0x0000}, /* background lookup */
    {"xvi-7.4h", 0x22d98032, 0x0200, 8, 0x0200},
    {"xvi-4.3l", 0xfd8b9d91, 0x0200, 9, 0x0000}, /* sprite lookup */
    {"xvi-5.3m", 0xbf906d82, 0x0200, 9, 0x0200},
    {"xvi-1.5n", 0x77245b66, 0x0100, 10, 0x0000}, /* sound waveforms */
    {"xvi-2.7n", 0x550f06bc, 0x0100, 10, 0x0100},
};

#define N(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

const gal_game_info_t gal_games[GAL_GAME_COUNT] = {
    [GAL_GAME_PACMAN] = {"Pac-Man", "PACMAN", "puckman", {"puckman.zip", "pacman.zip"}, pacman_files, N(pacman_files), 6, 0x3e,
                         {0x4000, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_GALAGA] = {"Galaga", "GALAGA", "galaga", {"galaga.zip"}, galaga_files, N(galaga_files), 9, 0x1f8,
                         {0x4000, 0x1000, 0x1000, 0x1000, 0x2000, 0x20, 0x100, 0x100, 0x100}},
    [GAL_GAME_DKONG] = {"Donkey Kong", "DONKEYKONG", "dkong", {"dkong.zip"}, dkong_files, N(dkong_files), 7, 0x7c,
                        {0x4000, 0x1000, 0x1000, 0x2000, 0x100, 0x100, 0x100}},
    [GAL_GAME_FROGGER] = {"Frogger", "FROGGER", "frogger", {"frogger.zip"}, frogger_files, N(frogger_files), 4, 0xc,
                          {0x3000, 0x1800, 0x1000, 0x20}},
    [GAL_GAME_DIGDUG] = {"Dig Dug", "DIGDUG", "digdug", {"digdug.zip"}, digdug_files, N(digdug_files), 11, 0x7e8,
                         {0x4000, 0x2000, 0x1000, 0x800, 0x1000, 0x1000, 0x4000, 0x20, 0x100, 0x100, 0x200}},
    [GAL_GAME_1942] = {"1942", "1942", "1942", {"1942.zip"}, g1942_files, N(g1942_files), 12, 0xfe0,
                       {0x8000, 0x4000, 0x4000, 0x4000, 0x4000, 0x2000, 0xc000, 0x10000, 0x300, 0x100, 0x300, 0x100}},
[GAL_GAME_ALIBABA] = {"Ali Baba", "ALIBABA", "alibaba", {"alibaba.zip"}, alibaba_files, N(alibaba_files), 7, 0x7c,
        {0x5800, 0x800, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_AMIDAR] = {"Amidar", "AMIDAR", "amidar", {"amidar.zip"}, amidar_files, N(amidar_files), 4, 0xc,
        {0x4000, 0x2000, 0x1000, 0x20}},
    [GAL_GAME_ANTEATER] = {"Anteater", "ANTEATER", "anteater", {"anteater.zip"}, anteater_files, N(anteater_files), 4, 0xc,
        {0x4000, 0x1000, 0x1000, 0x20}},
    [GAL_GAME_BAGMAN] = {"Bagman", "BAGMAN", "bagman", {"bagman.zip", "bagmanm2.zip"}, bagman_files, N(bagman_files), 3, 0x6,
        {0x6000, 0x4000, 0x20}},
    [GAL_GAME_BOMBJACK] = {"Bomb Jack", "BOMBJACK", "bombjack", {"bombjack.zip"}, bombjack_files, N(bombjack_files), 6, 0x2c,
        {0xa000, 0x2000, 0x3000, 0x6000, 0x1000, 0x6000}},
    [GAL_GAME_BNJ] = {"Bump 'n' Jump", "BUMPNJUMP", "brubber", {"brubber.zip", "bnj.zip"}, bnj_files, N(bnj_files), 4, 0xc,
        {0x6000, 0x1000, 0x6000, 0x2000}},
    [GAL_GAME_BTIME] = {"BurgerTime", "BURGERTIME", "btime", {"btime.zip"}, btime_files, N(btime_files), 5, 0xc,
        {0x4000, 0x1000, 0x6000, 0x1800, 0x800}},
    [GAL_GAME_CIRCUSC] = {"Circus Charlie", "CIRCUSCHARLIE", "circusc", {"circusc.zip"}, circusc_files, N(circusc_files), 7, 0x7c,
        {0xa000, 0x4000, 0x4000, 0xc000, 0x20, 0x100, 0x100}},
    [GAL_GAME_CRUSH] = {"Crush Roller", "CRUSHROLLER", "crush", {"crush.zip"}, crush_files, N(crush_files), 6, 0x3e,
        {0x4000, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_DKONG3] = {"Donkey Kong 3", "DONKEYKONG3", "dkong3", {"dkong3.zip"}, dkong3_files, N(dkong3_files), 7, 0x78,
        {0x8000, 0x2000, 0x2000, 0x2000, 0x4000, 0x400, 0x100}},
    [GAL_GAME_DKONGJR] = {"Donkey Kong Jr.", "DONKEYKONGJR", "dkongjr", {"dkongjr.zip", "dkongjrj.zip"}, dkongjr_files, N(dkongjr_files), 6, 0x3c,
        {0x6000, 0x1000, 0x2000, 0x2000, 0x200, 0x100}},
    [GAL_GAME_EYES] = {"Eyes", "EYES", "eyes", {"eyes.zip"}, eyes_files, N(eyes_files), 6, 0x3e,
        {0x4000, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_FANTASY] = {"Fantasy", "FANTASY", "fantasyu", {"fantasyu.zip", "fantasy.zip"}, fantasy_files, N(fantasy_files), 4, 0x4,
        {0x9000, 0x2000, 0x40, 0x1800}},
    [GAL_GAME_GALAXIAN] = {"Galaxian", "GALAXIAN", "galaxian", {"galaxian.zip"}, galaxian_files, N(galaxian_files), 3, 0x6,
        {0x2800, 0x1000, 0x20}},
    [GAL_GAME_GAPLUS] = {"Gaplus", "GAPLUS", "gaplus", {"gaplus.zip"}, gaplus_files, N(gaplus_files), 9, 0x1f8,
        {0x6000, 0x6000, 0x2000, 0x2000, 0x8000, 0x300, 0x100, 0x400, 0x100}},
    [GAL_GAME_GYRUSS] = {"Gyruss", "GYRUSS", "gyruss", {"gyruss.zip"}, gyruss_files, N(gyruss_files), 9, 0x1f0,
        {0x6000, 0x2000, 0x4000, 0x1000, 0x8000, 0x2000, 0x20, 0x100, 0x100}},
    [GAL_GAME_LADYBUG] = {"Lady Bug", "LADYBUG", "ladybug", {"ladybug.zip"}, ladybug_files, N(ladybug_files), 5, 0x1e,
        {0x6000, 0x2000, 0x2000, 0x20, 0x40}},
    [GAL_GAME_LIZWIZ] = {"Lizard Wizard", "LIZARDWIZARD", "lizwiz", {"lizwiz.zip"}, lizwiz_files, N(lizwiz_files), 6, 0x3e,
        {0x6000, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_MAPPY] = {"Mappy", "MAPPY", "mappy", {"mappy.zip"}, mappy_files, N(mappy_files), 8, 0xfc,
        {0x6000, 0x2000, 0x1000, 0x4000, 0x20, 0x100, 0x100, 0x100}},
    [GAL_GAME_MOONCRST] = {"Moon Cresta", "MOONCRESTA", "mooncrst", {"mooncrst.zip"}, mooncrst_files, N(mooncrst_files), 3, 0x6,
        {0x4000, 0x2000, 0x20}},
    [GAL_GAME_MRDO] = {"Mr. Do!", "MRDO", "mrdo", {"mrdo.zip"}, mrdo_files, N(mrdo_files), 6, 0x3e,
        {0x8000, 0x2000, 0x2000, 0x2000, 0x40, 0x20}},
    [GAL_GAME_MRTNT] = {"Mr. TNT", "MRTNT", "mrtnt", {"mrtnt.zip"}, mrtnt_files, N(mrtnt_files), 6, 0x3e,
        {0x4000, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_MSPACMAN] = {"Ms. Pac-Man", "MSPACMAN", "mspacman", {"mspacman.zip"}, mspacman_files, N(mspacman_files), 7, 0x7e,
        {0x4000, 0x2800, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_NIBBLER] = {"Nibbler", "NIBBLER", "nibbler", {"nibbler.zip"}, nibbler_files, N(nibbler_files), 4, 0x4,
        {0x9000, 0x2000, 0x40, 0x1800}},
    [GAL_GAME_PENGO] = {"Pengo", "PENGO", "pengo", {"pengo.zip"}, pengo_files, N(pengo_files), 5, 0x1e,
        {0x8000, 0x4000, 0x20, 0x400, 0x200}},
    [GAL_GAME_PHOENIX] = {"Phoenix", "PHOENIX", "phoenix", {"phoenix.zip"}, phoenix_files, N(phoenix_files), 4, 0xf,
        {0x4000, 0x1000, 0x1000, 0x200}},
    [GAL_GAME_PBACTION] = {"Pinball Action", "PINBALLACTION", "pbaction", {"pbaction.zip"}, pbaction_files, N(pbaction_files), 5, 0x1c,
        {0xa000, 0x2000, 0x6000, 0x10000, 0x6000}},
    [GAL_GAME_POOYAN] = {"Pooyan", "POOYAN", "pooyan", {"pooyan.zip"}, pooyan_files, N(pooyan_files), 7, 0x7c,
        {0x8000, 0x2000, 0x2000, 0x2000, 0x20, 0x100, 0x100}},
    [GAL_GAME_ROCNROPE] = {"Roc'n Rope", "ROCNROPE", "rocnrope", {"rocnrope.zip"}, rocnrope_files, N(rocnrope_files), 7, 0x7c,
        {0xa000, 0x2000, 0x4000, 0x8000, 0x20, 0x100, 0x100}},
    [GAL_GAME_SCRAMBLE] = {"Scramble", "SCRAMBLE", "scramble", {"scramble.zip"}, scramble_files, N(scramble_files), 4, 0xc,
        {0x4000, 0x1800, 0x1000, 0x20}},
    [GAL_GAME_INVADERS] = {"Space Invaders", "SPACEINVADERS", "invaders", {"invaders.zip"}, invaders_files, N(invaders_files), 1, 0x0,
        {0x2000}},
    [GAL_GAME_STARFORC] = {"Star Force", "STARFORCE", "starforc", {"starforc.zip"}, starforc_files, N(starforc_files), 8, 0x7c,
        {0x8000, 0x2000, 0x3000, 0x6000, 0x6000, 0x3000, 0xc000, 0x20}},
    [GAL_GAME_SCOBRA] = {"Super Cobra", "SUPERCOBRA", "scobra", {"scobra.zip"}, scobra_files, N(scobra_files), 4, 0xc,
        {0x6000, 0x1800, 0x1000, 0x20}},
    [GAL_GAME_THEGLOB] = {"The Glob", "THEGLOB", "suprglob", {"suprglob.zip", "theglobp.zip"}, theglob_files, N(theglob_files), 6, 0x3e,
        {0x4000, 0x1000, 0x1000, 0x20, 0x100, 0x200}},
    [GAL_GAME_TIMEPLT] = {"Time Pilot", "TIMEPILOT", "timeplt", {"timeplt.zip"}, timeplt_files, N(timeplt_files), 7, 0x7c,
        {0x6000, 0x1000, 0x2000, 0x4000, 0x40, 0x100, 0x100}},
    [GAL_GAME_TODRUAGA] = {"Tower of Druaga", "TOWEROFDRUAGA", "todruaga", {"todruaga.zip"}, todruaga_files, N(todruaga_files), 8, 0xfc,
        {0x8000, 0x2000, 0x1000, 0x4000, 0x20, 0x100, 0x400, 0x100}},
    [GAL_GAME_TURTLES] = {"Turtles", "TURTLES", "turtles", {"turtles.zip"}, turtles_files, N(turtles_files), 4, 0xc,
        {0x5000, 0x2000, 0x1000, 0x20}},
    [GAL_GAME_TUTANKHM] = {"Tutankham", "TUTANKHAM", "tutankhm", {"tutankhm.zip"}, tutankhm_files, N(tutankhm_files), 3, 0x0,
        {0x6000, 0x9000, 0x2000}},
    [GAL_GAME_VANVAN] = {"Van-Van Car", "VANVANCAR", "vanvan", {"vanvan.zip"}, vanvan_files, N(vanvan_files), 5, 0x1e,
        {0x5000, 0x1000, 0x1000, 0x20, 0x100}},
    [GAL_GAME_VANGUARD] = {"Vanguard", "VANGUARD", "vanguard", {"vanguard.zip"}, vanguard_files, N(vanguard_files), 4, 0x4,
        {0x8000, 0x1000, 0x40, 0x1000}},
    [GAL_GAME_XEVIOUS] = {"Xevious", "XEVIOUS", "xevious", {"xevious.zip"}, xevious_files, N(xevious_files), 11, 0x7b8,
        {0x4000, 0x2000, 0x1000, 0x1000, 0x2000, 0x7000, 0x4000, 0x300, 0x400, 0x400, 0x200}},
};
