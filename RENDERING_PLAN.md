# Rencana perbaikan rendering lintas platform

Tanggal: 4 November 2026, Asia/Jakarta.

Baseline sumber: branch `sdl2`, commit `81833c83ab99fad5b66bdde273242407c23006ea`.

Status: **rencana implementasi; belum ada milestone di bawah yang dinyatakan selesai**.

Dokumen ini menindaklanjuti penelusuran semua `RenderMode` pada Windows 32-bit,
Windows 64-bit, Linux 64-bit, Android ARM64, dan PS Vita ARM32. Baseline remote
diverifikasi kembali saat penyusunan dan masih berada pada commit di atas.
Dokumen ini tidak mengubah kode renderer, dependensi, resolusi, atau konfigurasi
game. Bukti review sebelumnya tidak dianggap sebagai hasil uji implementasi baru.

## 1. Sasaran dan batas pekerjaan

Ada dua hasil akhir yang harus dibedakan:

1. **Kestabilan dan kompatibilitas jalur yang ada:** resize aman, proyeksi benar,
   satu present per frame, akses struktur aman pada 32/64-bit, dan dukungan
   true-color software yang mempunyai kontrak serta pengujian jelas.
2. **Renderer GPU port yang berfungsi:** `RenderMode=OpenGL` pada Vita benar-benar
   menggambar sprite melalui vitaGL. Renderer GLES Android/Linux merupakan
   perluasan terpisah; SDL2 native dan presentasi framebuffer melalui GPU tidak
   dengan sendirinya memenuhi sasaran GPU sprite rendering.

Perbaikan inti tidak menunggu renderer Vita selesai. Sebaliknya, selesai
memperbaiki inti tidak boleh dilaporkan sebagai selesai mem-port OpenGL ke Vita.

Dokumen terkait:

- [SDL2_PLAN.md](SDL2_PLAN.md): migrasi desktop dan kesetaraan SDL/add-on.
- [port/android/PLAN.md](port/android/PLAN.md): backend SDL2 native Android dan
  kualifikasi APK/perangkat; bagian GPU Android tetap pekerjaan terpisah.
- [port/psvita/PLAN.md](port/psvita/PLAN.md): milestone migrasi Vita M0–M9.
- [AGENTS.md](AGENTS.md): fidelity, anotasi alamat asli, tempat artefak pengujian,
  dan perlindungan tree game referensi.

Saat implementasi menyelesaikan milestone, perbarui status dokumen terkait
dengan commit dan bukti. Jangan hanya menambah klaim selesai di README.

### 1.1 Kepemilikan build

| Entry point | Platform | Batas perubahan |
|---|---|---|
| Root `Makefile` | Windows/MinGW dan Linux | Backend desktop, objek, dependency kit, test host |
| Root `CMakeLists.txt` | Windows/MSVC 32-bit | Pertahankan profil SDL1 referensi; migrasi SDL2 MSVC bukan prasyarat patch keselamatan |
| `port/android/app/src/main/jni/src/CMakeLists.txt` dan `build.sh` | Android ARM64 | API SDL2 native; tidak mengaktifkan `MUGEN_VITA_GL` atau membawa kembali shim SDL1 |
| `port/psvita/CMakeLists.txt` dan helper CMake Vita | PS Vita | Semua pemilihan SDL, konteks vitaGL, SELF dan VPK berada di sini |

Root Makefile tidak menjadi build entry point Vita. CMake yang menggunakan
generator Make tetap berbeda dari root Makefile proyek.

### 1.2 Aturan yang harus tetap benar

- Pertahankan `GameWidth`, `GameHeight`, `localcoord`, dan pengaturan pengguna.
  Gunakan 640×480 untuk fixture utama; ukuran lain hanya fixture stress atau
  konfigurasi yang dipilih pengguna. Jangan menurunkan resolusi untuk menutupi
  masalah performa. Panel Vita 960×544 adalah ukuran output fisik, bukan izin
  mengganti koordinat game; default paket yang ada juga tidak diubah oleh plan ini.
- Pisahkan ukuran game, buffer CPU, tekstur, window, dan drawable/output fisik.
  Nilai-nilai tersebut tidak boleh disamakan hanya karena fixture awal 640×480.
- Pertahankan fidelity SDL1/Win32 sebagai referensi. Pisahkan port fix dari
  perluasan fitur. Jika perilaku asli memang cacat, dokumentasikan dan tempatkan
  perubahan pada profil port/fitur yang sesuai, bukan mengubah referensi diam-diam.
- Pertahankan urutan perhitungan floating point, urutan draw transparan, PalFX,
  premultiplication dan koordinat projective Q. Jangan menyelipkan optimisasi
  batching atau penggantian compiler flags dalam patch koreksi gambar.
- Jangan menambah field ke struktur rekonstruksi untuk state SDL/window/GPU baru.
  Gunakan state milik backend atau sidecar yang mempunyai lifecycle eksplisit.
- Jangan mengubah `install/`, shader referensi, maupun `mugen.cfg` asli saat
  pengujian. Gunakan salinan fixture atau hasil generate di `build/`.
- Semua scratch, raw image, log, hasil capture, dan test executable berada di
  subtree `build/` milik pekerjaan ini. Hapus artefak sementara milik sendiri.

## 2. Baseline yang harus dipertahankan dan diperbaiki

### 2.1 Tiga renderer, empat nilai konfigurasi

Startup: `System_Init -> SDL_Init -> Config_Load -> Gfx_SelectRenderer ->
Video_SetMode`, kemudian screen buffer dibuat dan menjadi render target.
Frame: `Sprite_Draw -> Screen_EndFrame -> Screen_Present -> g_gfxPresent`.
Android menambahkan flush `Android_PresentFrame` pada batas akhir frame.

| Nilai cfg | Target dan penggambaran baseline | Presentasi baseline |
|---|---|---|
| `System` | CPU, target RGB565, `Sprite_DrawSoft` | Surface melalui `Gfx_Present` |
| `DirectX` | Alias System dengan `VIDEO_DIRECTX` | Jalur System; bukan renderer sprite Direct3D tersendiri |
| `OpenGLScreen` | Software RGB565 yang sama, dikonversi ke RGBA | Satu tekstur layar dan quad fixed-function; `Gl_Init(0)` |
| `OpenGL` | Texture sprite, GLSL indexed/RGBA, FBO target 32-bit | `Gl_Present`; `Gl_Init(1)` |

System/OpenGLScreen memaksa depth 16 dan menonaktifkan StageFit/SystemFit pada
baseline. OpenGL memakai depth 32 dan ukuran video terpisah dari game.
`VRetrace` baseline hanya diteruskan ke jalur GL; mengubah kebijakan vsync System
adalah perubahan perilaku terpisah.

### 2.2 Matriks kemampuan saat ini

| Build | System/DirectX | OpenGLScreen | OpenGL |
|---|---|---|---|
| Win32 MSVC atau MinGW SDL1 | Software SDL1; DirectX hanya flag/hint legacy | Desktop compatibility GL | Desktop compatibility GL + GLSL/FBO |
| Win32 MinGW SDL2 | CPU ke SDL_Renderer/streaming texture | Desktop GL | Desktop GL + GLSL/FBO |
| Win64 MinGW SDL2 | Sama, tetapi audit ABI belum selesai | Desktop GL; audit ABI tetap berlaku | Desktop GL; audit ABI tetap berlaku |
| Linux x64/ARM64 SDL2 | CPU ke SDL_Renderer; tidak ada driver DirectX | Memerlukan desktop GL | Memerlukan desktop compatibility GL; belum ada backend GLES |
| Android ARM64 native SDL2 | System; SDL_Renderer menyajikan framebuffer CPU | Dipaksa ke System | Dipaksa ke System |
| Vita ARM32 SDL1 | Software, scaler Vita dan SDL_Flip | Belum operasional; fallback dicoba | Belum operasional; fallback dicoba |

Matriks ini mendeskripsikan sumber, bukan sertifikasi visual perangkat.

### 2.3 Daftar masalah dan bukti awal

| ID | Masalah | Bukti review | Tahap penutupan |
|---|---|---|---|
| R01 | Resize tekstur gagal, tetapi surface dan metadata berubah | 640×480 ke 320×240 meninggalkan tekstur 640×480; upload dengan buffer kecil memicu SIGSEGV pada SDL2 asli dengan guard page | M1 |
| R02 | Proyeksi OpenGLScreen terakumulasi | `gluOrtho2D` tanpa `glLoadIdentity`; resize berulang mengalikan matriks sebelumnya | M2 |
| R03 | Present ganda pada System/PageFlip SDL2 desktop | Satu `Gfx_Present`: Normal menghasilkan 1 present, PageFlip 2 | M3 |
| R04 | Kamera membaca layout 32-bit pada target 64-bit | `Bg.legacy`: offset 0x64 pada 32-bit, 0x7c pada 64-bit; `camera.c` membaca 0x64 | M4 |
| R05 | Jalur software melewati sprite true-color dan surface dengan transform tertentu | Sprite 24/32-bit non-SDL_Surface langsung return; surface transform/blend tidak masuk blit sederhana | M5 |
| R06 | DirectX dan fallback tidak menyatakan backend efektif dengan jelas | Environment driver ditulis sesudah SDL video init; Android mengubah mode GL menjadi System | M6 |
| R07 | OpenGL Vita belum mempunyai jalur lengkap | Konteks/swap belum terhubung, gate NPOT berbeda nama, ARB_fragment_program tidak tersedia, immediate mode tidak memakai program sprite | M7 |
| R08 | GLES Android/Linux belum tersedia | Shader/fixed-function desktop bukan API GLES; Android mengecualikan glrender/gldetect | M8, fitur terpisah |

Probe review menggunakan fungsi yang diekstrak dengan instrumentasi dan beberapa
uji SDL2 runtime asli pada driver dummy. Probe tersebut tidak menggantikan
regression test yang mengompilasi translation unit produksi dengan header SDK
normal. Hasil review tidak membuktikan APK/VPK, determinisme seluruh game,
kebenaran setiap efek, atau kualitas visual GPU.

## 3. Urutan implementasi dan dependensi

| Milestone | Deliverable | Bergantung pada | Ukuran relatif |
|---|---|---|---|
| M0 | Baseline, fixture dan reproduksi permanen | Tidak ada | Kecil–sedang |
| M1 | Transaksi resource video dan error path aman | M0 | Sedang |
| M2 | Resize/proyeksi GL idempoten | M0; gunakan kontrak M1 | Sedang |
| M3 | Satu batas present per frame | M0 | Sedang |
| M4 | Akses field dan lifecycle aman pada 64-bit | M0 | Sedang; audit lanjutan dapat bertambah |
| M5 | True-color software yang teruji | M0, M4; M1/M3 untuk kualifikasi end-to-end | Besar |
| M6 | Seleksi mode, capability dan fallback yang jujur | M1–M4 | Sedang |
| M7 | SDL2/VitaGL programmable renderer | M0, M4, M6 dan milestone Vita M0–M9 terkait | Sangat besar |
| M8 | Renderer GLES Android/Linux | M0, M4, M6; reuse bagian teruji M7 bila cocok | Besar; tidak memblokir patch inti |
| M9 | Kualifikasi, dokumentasi dan release gate | Milestone yang diklaim dalam rilis | Sedang–besar |

Ukuran di atas bukan estimasi tanggal. Perkiraan waktu baru dibuat setelah M0
dan bukti device/toolchain tersedia. Kerjakan patch keselamatan M1 sebelum
optimisasi, penambahan fitur, atau perubahan default backend.

### 3.1 Peta perubahan file

| Kelompok file | Tanggung jawab perubahan | Milestone |
|---|---|---|
| `src/sdl2compat.c/.h` | Transaksi surface/texture, metadata, retry/reset, dirty/flush desktop | M1, M3 |
| `src/video.c`, `src/events.c` | Resize/projection, drawable, init failure dan event idempotence | M1, M2 |
| `src/gfx.c`, `src/prim.c`, `src/frame.c` | Komposisi, update surface offscreen, satu frame boundary, capture | M3 |
| `src/camera.c`, `src/bg.h` | Field legacy, layout dan akses kamera bertipe | M4 |
| `src/char.c`, `src/playerstate.c/.h`, `src/game.h` | Projectile/afterimage dan ownership typed | M4 |
| `src/sprite.c`, `src/softrender.c`, `src/sff.c`, `src/spritecodec.c`, `src/palette.c` | Decode/cache dan rasterizer true-color dengan efek | M5 |
| `src/config.c`, `src/system.c`, `src/globals.c`, `src/mugen.h`, `src/symbols.h` | Requested/effective mode, startup, capability dan dispatch table | M6–M8 |
| `src/glrender.c`, `src/gldetect.c` | Kontrak renderer desktop, geometry/PalFX reference, calibration | M2, M7, M8 |
| `port/android/app/src/main/jni/src/` dan CMake-nya | Presenter native/lifecycle; backend GLES baru hanya pada M8 | M1, M3, M8 |
| `port/psvita/src/`, `port/psvita/shaders/`, CMake Vita dan `cmake/VitaGl.cmake` | Provider/context, programmable draw, shader, target dan VPK | M7 |
| `tools/`, `port/android/tests/`, validation/CI yang sesuai | Harness produksi, fault injection, golden/counter dan device probes | M0, M9 |
| `SDL2_PLAN.md`, kedua plan/README port, panduan build, `AGENTS.md` | Status, kemampuan aktual, nama output dan bukti gate | Setiap milestone terkait |

Backend/provider/helper baru adalah file yang akan ditambahkan dalam milestone
terkait, bukan file yang dianggap sudah ada. Source bersama baru harus didaftarkan
pada source list CMake yang eksplisit serta profil Make/Android/Vita yang relevan.
Jika root CMake MSVC ikut mengompilasinya, gunakan guards yang benar agar profil
SDL1 referensi tidak memerlukan header/library port atau GPU API baru.

## 4. M0 — Baseline dan regression harness

Harness tinggal di `tools/test_render_baseline.c` (satu sumber, dua build:
SDL1 + SDL_gfx seperti test_zoomdiff) dengan artefak di
`build/render-baseline/<target>/`. Perintah: `tools/test_render_baseline.sh`,
yang menulis `baseline.json` (item 1) dan `counters.json` (item 5) di samping
capture biner. Semua scratch mengikuti aturan AGENTS.md: di dalam proyek,
`build/` saja.

Gate regresi R01–R05 tinggal di tiga harness (semua lolos pada implementasi
saat ini, semua gagal pada implementasi pra-perbaikan yang sesuai):

- `tools/test_render_regress.sh` → `build/render-regress/`: R01 (transaksi
  resize via `MUGEN_SDL2_DIAG` + injeksi `Sdl2_DiagFailNext`, cek independen
  `SDL_QueryTexture`) dan R03 (satu present per shim call via counter
  diagnostik). Link `src/sdl2compat.c` + `src/sdl2surface.c` produksi.
- `tools/test_render_video.sh` → `build/render-video/`: R02 (`Video_OnResize`
  produksi dengan GL context nyata, 20 resize identik → matriks ortho yang
  diharapkan, bukan hitungan API call). Link `src/video.c` produksi.
- `tools/test_render_host.sh` → `build/render-host/`: R04 (assert
  `offsetof` referensi 32-bit + sentinel tetangga) dan R05 (sprite BGRA 2x1
  → RGB565 exact, transparan vs tak-tergambar dibedakan via prefill).
  Link `src/sprite.c` + `src/keyedarray.c` + `src/softrender.c` produksi.

Diagnostik `MUGEN_SDL2_DIAG` (`Sdl2_DiagReset/Counters/TextureSize/FailNext`)
hanya ada di build test; build produksi tidak berubah. Batas M0 yang
tercatat: gate ABI01/ABI02 64-bit penuh butuh compiler target nyata (todo
M4); matriks viewport/scale dan pacing (§12–13 plan awal) masuk M9.

### Pekerjaan

1. Catat SHA kode, SHA dependensi, compiler/triple, flags, SDL major/minor,
   driver video/renderer, GPU, OS, ukuran game/window/drawable, dan cfg efektif.
2. Simpan build SDL1/Win32 referensi dan baseline SDL2 sebelum perubahan pada
   direktori berbeda. Jangan menggunakan satu direktori objek untuk dua target.
3. Buat fixture minimal bebas aset pihak ketiga: ramp RGB565, pola per baris,
   indexed8 dengan palette nontrivial, PNG RGB/RGBA kecil, dan quad berkoordinat
   yang hasilnya dapat dihitung secara independen.
4. Ubah reproduksi R01–R05 menjadi test produksi. Gunakan wrapper alokasi/ops
   yang tersedia dalam build test pada GCC maupun MSVC; jangan bergantung hanya
   pada GNU `--wrap`, header buatan, atau salinan badan fungsi.
5. Tambahkan penghitung `draw`, `upload`, `present`, `swap`, `resource create`,
   `destroy`, dan `fallback` pada build diagnostik. Counter tidak mengubah
   semantik renderer atau menulis log per pixel pada build normal.
6. Untuk perilaku yang diduga merupakan quirk asli, bandingkan executable asli
   dan rekonstruksi pada skenario sama sebelum mengubah referensi SDL1.

### Kriteria selesai

- [ ] R01 gagal pada kondisi alokasi yang diinjeksikan, bukan karena setup test.
- [ ] R02 mendeteksi proyeksi yang salah tanpa sekadar menghitung panggilan API.
- [ ] R03 mendeteksi present ganda dan present dari update surface offscreen.
- [ ] R04 membedakan field tetangga dari `legacy`, bukan hanya mencetak sizeof.
- [ ] R05 mempunyai expected image yang bukan constant fill dan membedakan
  sprite transparan dari sprite yang tidak pernah digambar.
- [ ] Mematikan perbaikan terkait di kemudian hari membuat test kembali gagal.

## 5. M1 — Resize/resource video sebagai transaksi

File utama: `src/sdl2compat.c/.h`, `src/video.c`, `src/events.c`.
Android: pastikan kontrak `android_video.c` tetap aman; jangan menggantinya
dengan desktop SDL1 compatibility layer.

### 5.1 Invariant resource

Untuk setiap upload penuh yang diterima:

- format, lebar dan tinggi tekstur cocok dengan buffer sumber yang dipublikasikan;
- pitch berasal dari surface sebenarnya, termasuk padding, dan memenuhi panjang
  row; ukuran alokasi dihitung dengan overflow check menggunakan `size_t`;
- pointer publik `g_videoSurface`/`g_screenSurface` menunjuk resource hidup;
- state ukuran yang tercatat menjelaskan resource yang benar-benar dimiliki;
- texture dan renderer hidup pada window/backend yang sama.

Jangan memvalidasi hanya `width == texture_w` pada metadata yang baru ditulis.
Pada build test, gunakan `SDL_QueryTexture` sebagai pemeriksaan independen.

### 5.2 Urutan perubahan

1. Pisahkan ukuran output fisik dari ukuran storage buffer. Resize window oleh
   OS tidak berarti framebuffer game harus direalokasi atau koordinat game berubah.
2. Validasi request dimensi/format dan batas tekstur sebelum mutasi resource.
   Event minimized/ukuran nol tidak boleh dialihkan menjadi buffer game 1×1.
3. Buat candidate surface dan candidate texture pada variabel lokal. Validasi
   format dan semua alokasi sebelum mengubah layout renderer aktif. Simpan
   logical size/viewport lama; bila pengaturan layout candidate gagal, pulihkan
   layout lama juga. Tangani setiap return error yang relevan.
4. Commit pointer, ukuran dan format hanya ketika seluruh resource wajib siap.
   Caller mempublikasikan alias global hanya sesudah commit berhasil.
5. Setelah commit, bebaskan pasangan lama tepat sekali. Pada kegagalan, hapus
   candidate yang sempat dibuat dan pertahankan pasangan serta alias lama.
6. Jika window fisik sudah terlanjur berubah, render framebuffer lama dengan
   scaling/letterbox yang konsisten. Jangan mengaku ukuran storage mengikuti
   window jika alokasinya gagal. Rollback ukuran window bukan syarat keselamatan.
7. Kegagalan awal tanpa resource lama harus menghasilkan error yang jelas dan
   pointer NULL; caller tidak meneruskan `Gl_Init` atau draw dengan mode gagal.
8. Metadata texture_w/texture_h hanya berubah setelah penggantian sukses.
   Request ukuran yang sama sesudah failure harus mencoba lagi bila perlu.
9. GPU/device reset membatalkan resource sesuai kontrak backend dan menciptakan
   pasangan valid sebelum upload berikutnya. Retry tidak memakai handle mati.
10. Tetapkan pemilik surface: backend memiliki display surface; engine meminjam
    pointer display; `SpriteSet` memiliki surface screen/sprite buatannya sendiri.
    Mode failure dan shutdown tidak boleh menyebabkan double free atau alias basi.

### Pengujian dan gate

- [ ] Shrink, grow, format/depth change, ukuran sama, repeated resize, reset,
  minimize/restore, fullscreen, dan shutdown setelah failure.
- [ ] Injeksi gagal pada setiap alokasi, termasuk surface, texture, renderer dan
  placeholder GL; cek pointer, dimensi aktual, jumlah destroy dan retry.
- [ ] Guard-page/ASan membuktikan upload tidak membaca melewati buffer.
- [ ] Surface berpading, format 16/32-bit, dan row dengan warna berbeda tetap benar.
- [ ] Kegagalan SetVideoMode pada GL SDL2 berhenti sebelum mengakses konteks atau
  surface NULL. Perlakuan quirk SDL1 referensi ditentukan oleh hasil M0.
- [ ] Recovery kembali menggambar pixel yang benar; log “success” saja tidak cukup.

## 6. M2 — Proyeksi, viewport dan resize GL yang idempoten

File utama: `src/video.c::Video_OnResize`, `src/gfx.c::Gfx_PresentGlScreen`,
`src/glrender.c::Gl_SetupView`, dan pemetaan event resize di `src/events.c`.

1. Buat helper reset proyeksi OpenGLScreen yang eksplisit: pilih projection,
   `glLoadIdentity`, tetapkan ortho untuk koordinat quad, lalu pilih modelview
   dan reset transform yang dimiliki presenter. Jangan mengalikan matriks lama.
2. Jangan memakai ukuran render target game sebagai pengganti ukuran quad output.
   Bedakan logical quad dimensions dari drawable pixels untuk viewport.
3. Pada SDL2 GL, gunakan ukuran drawable aktual untuk `glViewport`. Pada SDL1
   gunakan kontrak surface/video yang ada; jangan memanggil API SDL2 di profil SDL1.
4. Event RESIZED dan SIZE_CHANGED dengan state efektif sama harus aman diulang.
   Deduplicate pekerjaan mahal jika perlu, tetapi jangan mengabaikan perubahan
   drawable/DPI hanya karena ukuran logical window tidak berubah.
5. Ukuran nol ketika minimized menunda draw. Restore menghasilkan satu setup
   valid tanpa mengubah GameWidth/GameHeight atau ukuran texture sprite/FBO game.
6. Pertahankan konteks GL dan resource sprite saat resize normal. Mode change
   antarbeberapa jenis backend mempunyai jalur teardown/rebuild tersendiri.
7. Nyatakan dan pulihkan state matrix/FBO/scissor yang disentuh presenter agar
   overlay, screenshot, dan draw berikutnya tidak mewarisi state yang salah.
8. Cocokkan perilaku SDL1 dengan referensi asli. Jika akumulasi matriks merupakan
   quirk referensi, gunakan koreksi profil port dan catat perbedaannya.

Gate:

- [ ] Dua puluh resize identik menghasilkan matriks, viewport dan gambar identik.
- [ ] 640×480 ke 800×600 lalu kembali ke 640×480 mengisi area yang semestinya.
- [ ] Uji nonuniform aspect, high-DPI, fullscreen, minimize dan repeated restore.
- [ ] Penanda empat sudut, checkerboard, screenshot dan overlay tetap sejajar.
- [ ] Resource sprite/FBO tidak dibuat ulang hanya karena window resize.

## 7. M3 — Satu present pada batas frame

File utama: `src/gfx.c`, `src/prim.c`, `src/frame.c`, dan `src/sdl2compat.c/.h`.

### Kontrak

Untuk frame yang digambar dan display-nya berubah, backend System SDL2 melakukan
paling banyak satu upload framebuffer final dan satu present pada main/render
thread. Frame yang dilewati tidak melakukan upload/present biasa. Recovery redraw
karena expose/resume merupakan request eksplisit dan tercatat terpisah.

### Perubahan

Status implementasi (commit M3): langkah 1–2, 4–5 dan 8 selesai.

1. ~~Tambahkan API backend untuk dirty marking dan frame-end flush~~ SELESAI:
   `Sdl2_MarkDisplayDirty`/`Sdl2_PresentFrame` di `src/sdl2compat.c/.h`
   (diagnostik eksplisit + flush batas frame).
2. ~~`SDL_UpdateRect` di desktop shim tidak langsung mempresentasikan~~
   SELESAI: hanya menandai dirty bila surface-nya display; offscreen no-op
   untuk presentasi. Beda display vs offscreen via perbandingan pointer
   surface (§7.2).
3. Audit `Gfx_Present` dan primitive di `prim.c`: TERBUKA — 1000-primitive
   gate ada di harness (`test_render_regress`), audit lengkap menyusul M9.
4. ~~Flush desktop SDL2 di batas frame~~ SELESAI: `Screen_Present`
   memanggil `Sdl2_PresentFrame()` (guarded `MUGEN_SDL2`, Android
   dikecualikan — pakai `Android_PresentFrame` sendiri). Mencakup semua
   pemanggil `Screen_Present` (gameloop, title, matchload, round).
   Skip-frame return sebelum `g_gfxPresent` → tidak mempresentasikan.
5. ~~Rekonsiliasi `SDL_Flip` vs PageFlip~~ SELESAI: UpdateRect-dirty +
   Flip-present = satu present; workaround skip-UpdateRect di `gfx.c`
   dihapus (tidak diperlukan lagi).
6. Pisahkan flush System dari swap GL: TERBUKA (langkah M9) — `Gl_Present`/
   OpenGLScreen tidak tersentuh perubahan ini.
7. Jalur SDL1 dan Vita SDL1: TIDAK BERUBAH (semua perubahan di-guarded
   `MUGEN_SDL2`; Vita pakai `SDL_Flip` sendiri).
8. ~~Penggunaan di luar loop frame~~ SELESAI: `Sdl2_MarkDisplayDirty`
   eksplisit untuk diagnostik; tidak ada lagi immediate present global.
9. Vsync System tidak diubah (langkah M9 — pacing terpisah).
2. `SDL_UpdateRect` di desktop shim tidak langsung mempresentasikan seluruh window
   untuk setiap permintaan. Bedakan display surface dari surface offscreen.
3. Audit `Gfx_Present` dan primitive di `prim.c`: update satu pixel pada render
   target offscreen tidak boleh menyebabkan upload display sebelum komposisi.
4. Setelah komposisi System selesai, tandai display dirty. Flush desktop SDL2 di
   batas frame seperti prinsip Android, tanpa menambahkan flush kedua ke Android.
5. Rekonsiliasi arti `SDL_Flip` dan `BlitMode=PageFlip` dalam shim; jangan
   mempertahankan UpdateRect-present diikuti Flip-present untuk buffer sama.
6. Pisahkan flush System dari swap GL. `Gl_Present`/OpenGLScreen tetap mempunyai
   tepat satu pemilik swap; frame-end System tidak melakukan swap tambahan.
7. Pertahankan jalur SDL1 dan kebutuhan `SDL_Flip` pada software Vita SDL1 yang
   benar-benar double-buffered. Jangan menghapus flip Vita karena desktop memakai
   SDL_Renderer atau menganggap UpdateRect melakukan present pada Vita.
8. Inventarisasi penggunaan di luar loop frame. Jika diagnostik perlu present
   langsung, sediakan fungsi eksplisit; jangan menjadikan semua update pixel
   sebagai immediate present lagi.
9. Jangan mengaktifkan vsync System secara tidak sengaja. Pengujian VRetrace dan
   PageFlip mencatat present count, frame time dan kebijakan pacing secara terpisah.

Gate:

- [ ] Normal dan PageFlip menghasilkan satu upload/present untuk frame final sama.
- [ ] Seribu primitive update offscreen menghasilkan nol present sebelum frame-end.
- [ ] Skip-frame, repeated dirty mark, pre-present overlay, expose, suspend/resume,
  failed present/retry, dan GPU reset mempunyai counter yang dapat diperiksa.
- [ ] Screenshot berasal dari frame/target yang dimaksud, bukan backbuffer yang
  sudah invalid setelah present. Tidak mengasumsikan isi backbuffer bertahan.
- [ ] Android mempertahankan hasil host test coalescing yang sudah ada.

## 8. M4 — Audit ABI dan akses struktur 32/64-bit

File utama: `src/camera.c`, `src/char.c`, `src/bg.h`, `src/playerstate.h`,
`src/playerstate.c`, dan situs raw-offset yang ditemukan saat audit.

1. Ganti pembacaan kamera `bg + 0x64` dengan `bg->legacy`, tanpa mengubah urutan
   perhitungan kamera atau kebijakan pembulatan yang dipilih field tersebut.
2. Inventarisasi pointer arithmetic, memcpy ukuran literal, cast pointer ke
   int/long, penyimpanan alamat pada slot 32-bit, serta fungsi dengan argumen
   ukuran yang diasumsikan 32-bit. Audit LP64 Linux/Android dan LLP64 Windows.
3. Audit reset projectile di `char.c`: baseline menganggap alamat `proj+0x504`
   sebagai `KeyedArray **`, sementara deklarasi field adalah `AfterImage *`.
   Ini bukan sekadar mengganti angka offset dengan offsetof.
4. Telusuri ownership `AfterImage` dan removal canonical di
   `playerstate.c::Projectiles_Remove`. Gunakan tipe dan helper lifecycle yang
   benar setelah dibandingkan dengan perilaku referensi; jangan menulis field
   KeyedArray ke objek AfterImage atau membebaskan frame yang masih dimiliki pool.
5. Untuk situs seperti `BgElem + 0x88`, ukur dan cocokkan anggota sebenarnya.
   Offset yang kebetulan sama pada dua ABI tetap perlu didokumentasikan; jangan
   menyatakan semua literal heksadesimal sebagai pointer bug.
6. `GlTex` berisi field 32-bit, bukan pointer. Verifikasi ukuran 0x1C dan anggota
   yang diakses sebelum mengubahnya. Jangan mengganti GL object name dengan
   pointer-sized type hanya karena host 64-bit.
7. Pertahankan `MUGEN_ASSERT_SIZE/OFFSET` profil referensi 32-bit. Untuk port,
   tambahkan pemeriksaan semantik/offset yang memang diperlukan dan laporan
   layout; menonaktifkan assertion tidak menjadi bukti kebenaran.
8. State backend baru, function pointer, allocation size, alignment dan resource
   handle diuji dengan compiler target sebenarnya; build `-m64` di host saja
   tidak memverifikasi ABI Windows maupun ARM64.

Gate:

- [ ] Bg dengan nilai `legacy` berlawanan dari field tetangganya memilih cabang benar.
- [ ] Offset referensi 32-bit tetap cocok; jalur 64-bit membaca field yang benar.
- [ ] Stage legacy/nonlegacy dengan kamera fractional dan zoom tetap konsisten.
- [ ] Projectile ber-afterimage dibuat, dihapus, di-reset dan di-reload berulang
  tanpa invalid write, dangling reference, atau perubahan ownership pool.
- [ ] Sanitizer host bersih untuk skenario tersebut; Win64 dan Android ARM64
  menjalankan skenario, bukan hanya berhasil link.

## 9. M5 — Dukungan true-color software tanpa silent drop

File utama: `src/sprite.c`, `src/softrender.c`, `src/sff.c`, `src/spritecodec.c`,
`src/palette.c`, dan helper decode/cache yang benar-benar dipakai baseline.

### 9.1 Batas semantik

Target utama tetap framebuffer RGB565. Dukungan source 24/32-bit tidak berarti
semua primitive dan rasterizer sudah mendukung target 32-bit. Tes membuat display
32-bit pada Android hanya membuktikan presentasi, bukan kemampuan seluruh engine
menggambar ke target tersebut.

Perluasan ini ditandai sebagai fitur port. Pertahankan perilaku referensi legacy
melalui pemilihan profil internal yang eksplisit sampai hasilnya disetujui.
Jangan mengubah semua build SDL1 tanpa bukti dan catatan fidelity.

### 9.2 Implementasi bertahap

1. Telusuri RGB/RGBA dari node SFF, lazy decode, cache, `Sprite_Init`, linked
   sprite, hingga draw. Format compressed 10/11/12 dan RAW tidak boleh ditafsirkan
   sebagai SDL_Surface hanya karena bpp-nya 32.
2. Buat descriptor source yang menyatakan format/channel masks, pitch, alpha,
   premultiplication, ownership, ukuran, dan key/mask. Reuse decoder yang ada;
   jangan membuat salinan penuh sprite setiap draw.
3. Tetapkan kapan source didecode dan kapan cache invalid. Linked sprite tidak
   boleh mengambil ownership baru atau meninggalkan pointer hasil decode basi.
4. Implementasikan opaque RGB24/RGBA32 ke RGB565 terlebih dahulu: clipping,
   axis/pivot, row padding, color-channel mapping dan quantization harus benar.
5. Tambahkan source alpha, masking, add/subtract dan pasangan alpha source/dest
   mengikuti persamaan engine. Tentukan apakah data masuk straight/premultiplied
   berdasarkan decoder, bukan asumsi nama format.
6. Tambahkan scale, flip, shear, rotation, clipping setelah transform, stretch
   quad/projective sampling, dan PalFX dalam urutan yang sama dengan reference.
7. Bentuk cache/scratch buffer yang dapat dipakai ulang. Perubahan palette/PalFX
   tidak boleh menyebabkan reuse pixel hasil efek lama secara keliru.
8. Untuk operasi yang belum didukung, kembalikan status capability/diagnostik
   sekali per jenis operasi; jangan diam-diam melewati sprite dan melaporkan
   dukungan true-color lengkap.
9. Periksa font solid/blended dan surface sprite yang sudah bekerja. Font RGBA
   dan sprite SFF RGBA adalah dua jalur berbeda dan memiliki test terpisah.

### Referensi gambar dan gate

- [ ] Fixture 24/32-bit compressed dan RAW, linked sprite, palette/key, serta
  alpha 0/128/255 menghasilkan gambar non-kosong yang benar.
- [ ] RGB565 expected value dihitung dari masks secara independen. Decoder
  pembanding tidak menggunakan shift channel hardcoded yang salah.
- [ ] Opaque/untransformed dan kasus integer yang ditetapkan harus pixel-exact.
- [ ] Untuk edge transform, gunakan reference GPU desktop/rumus independen dan
  tentukan toleransi setelah pengukuran; tidak ada exemption gambar seluruhnya.
- [ ] PalFX, alpha source/dest, add/sub, flip, scale, rotate dan clipping diuji
  terhadap expected image, bukan hanya return value atau nonzero draw count.
- [ ] Karakter/stage SFF true-color berjalan pada System Android dan Vita.
- [ ] Kasus indexed8, font dan skenario referensi tidak mengalami regresi.

## 10. M6 — Seleksi renderer, DirectX dan fallback yang jujur

1. Simpan requested mode, effective renderer, SDL video driver, SDL_Renderer
   driver atau GL provider sebagai konsep berbeda pada state backend/log.
2. Pertahankan nilai cfg lama dan enum yang dipakai engine. Jangan mengganti arti
   `OpenGLScreen` menjadi GPU sprite renderer karena platform memakai GPU untuk
   menampilkan tekstur layar.
3. Untuk SDL2, perlakukan `DirectX` sebagai alias kompatibilitas System yang
   terdokumentasi, bukan jaminan renderer Direct3D. Jangan mengirim nilai driver
   SDL1 `directx` ke startup SDL2 atau memaksakan Direct3D pada Linux/mobile.
4. Jika pemilihan driver eksplisit kelak diperlukan, pisahkan SDL video driver
   dari SDL_Renderer driver dan lakukan sebelum subsystem/renderer dibuat.
   Jangan memindahkan seluruh `Config_Load` sebelum SDL_Init: loader konfigurasi
   juga memicu pekerjaan lain yang mempunyai dependensi startup.
5. Untuk SDL1/Win32, rekam perilaku asli late driver hint sebelum memutuskan
   mengoreksinya. Koreksi opt-in dan klarifikasi dokumentasi lebih aman daripada
   mengubah fidelity seluruh startup dalam patch rendering.
6. Definisikan capability dari fungsi yang tersedia: System, true-color software,
   GPU sprite, GLScreen, capture, FBO, blend, device reset dan context loss.
   Macro target atau keberhasilan link tidak cukup.
7. Validasi mode sebelum memilih tabel fungsi dan sebelum asset GPU dimuat.
   Tabel NULL/stub tidak boleh dapat dipilih pada profil yang mengeklaim mode itu.
8. Normal recovery boleh fallback dengan pesan requested/effective dan alasan.
   Qualification memakai strict mode yang gagal bila renderer diminta tidak aktif.
   Gunakan opsi Vita yang sudah direncanakan; padanan desktop/Android masih usulan.
9. Jika backend harus berganti sesudah asset dimuat, teardown dan reload resource
   dengan ownership benar. Jangan sekadar mengganti `g_renderMode` sehingga
   texture sprite diperlakukan sebagai surface/raw data.
10. Log backend efektif sekali saat startup dan saat recovery/mode berubah. Hindari
    noise per frame yang merusak log comparison atau performa.

Gate: seluruh nilai cfg, nilai invalid, strict/fallback, missing GL context,
missing shader, extension alias, dan unavailable GPU menghasilkan keputusan
serta resource yang konsisten. Fallback tidak dihitung sebagai kelulusan GPU.

## 11. M7 — SDL2 dan renderer programmable VitaGL untuk PS Vita

Ini bukan patch shader kecil. Eksekusi detail mengikuti
[port/psvita/PLAN.md](port/psvita/PLAN.md), khususnya M1–M3 dan M5–M9, dengan
perbaikan inti dokumen ini sebagai prasyarat. Jangan mengaktifkan `ANDROID_PORT`
pada Vita. Nama opsi berikut adalah **usulan dari plan Vita, belum API baseline**:
`MUGEN_VITA_SDL_BACKEND`, `MUGEN_VITA_SDL2_PROVIDER`,
`MUGEN_VITA_GL_RENDERER`, `MUGEN_VITA_RENDERER_STRICT` dan dependency prefix.

### V1 — Dependency, link dan satu pemilik GPU

- [ ] Buat profil CMake SDL1 dan SDL2 yang memakai direktori objek/dependency
  berbeda; pertahankan SDL1 recovery sampai kualifikasi perangkat selesai.
- [ ] Pin SDL2, SDL2_ttf, SDL2_mixer, codec dan vitaGL; catat commit, build flags,
  ABI, .pc file dan isi final link map. Host library tidak boleh bocor ke Vita.
- [ ] `MUGEN_VITA_BUILD_VITAGL` harus memilih target vendored dalam executable
  final, bukan sekadar membangun library yang tidak digunakan.
- [ ] Android tetap SDL2 native tanpa desktop shim. Untuk profil Vita SDL2,
  putuskan adapter native/reuse di gate ini: reuse helper key/scaler/stream yang
  teruji, tetapi jangan mengaktifkan window/present desktop shim tanpa pemetaan
  provider Vita yang nyata. Catat keputusan yang menyempurnakan plan Vita lama.
- [ ] Pilih satu pemilik GXM/display/context. Opsi utama: provider SDL2-vitaGL
  teruji yang benar-benar menerapkan kontrak context/swap SDL_GL.
- [ ] Alternatif bila provider itu belum tersedia: backend Vita memiliki
  `vglInitExtended`/`vglSwapBuffers` sendiri dan SDL2 hanya subsystem yang tidak
  mengaktifkan driver video GXM bersamaan. Buktikan konfigurasi tersebut pada
  hardware sebelum menghubungkannya ke engine.
- [ ] Jangan memanggil manual vgl init sesudah stock SDL video driver sudah
  mengambil GXM. Fallback dari GL ke System harus melepaskan owner lama terlebih
  dahulu; kegagalan teardown tidak boleh diteruskan sebagai sukses recovery.

### V2 — Capability probe fungsional

- [ ] Buat konteks, clear/capture warna, triangle berprogram custom, texture 2D,
  palette 256×1, FBO, clipping dan swap dengan hasil pixel independen.
- [ ] Parser versi tetap mengenali string VitaGL; ES 2.0 bukan otomatis bukti
  semua entry point desktop GL 2.0 tersedia.
- [ ] Terima nama OES/ARB NPOT hanya untuk kemampuan yang benar-benar dibutuhkan
  dan diuji. Jangan mensyaratkan `ARB_fragment_program` jika jalur baru hanya
  memakai shader programmable lain yang telah lulus compile/link/draw.
- [ ] Periksa format attachment/depth dan FBO completeness; context yang berhasil
  dibuat tetapi shader atau FBO gagal tidak memenuhi OpenGL mode.

### V3 — Submission vertex dan shader

- [ ] Implementasikan vertex buffer/attribute eksplisit untuk indexed dan RGBA;
  hapus ketergantungan `glBegin/glEnd` dari draw path programmable Vita.
- [ ] Reuse perhitungan quad/transform CPU dengan urutan float yang sama. Bawa
  position dan texcoord sebagai empat komponen bila diperlukan; jangan membuang Q.
- [ ] Gunakan MVP/attribute/varying yang benar-benar disuplai. Shader baru yang
  mendeklarasikan attribute tidak cukup bila submitter masih fixed-function.
- [ ] Pakai palette texture 2D 256×1 dengan nearest sampling dan mapping index
  yang diuji untuk 0, 1, 254, 255 serta semua entri. Texture1D emulation bukan
  pengganti shader sampler2D dan binding yang konsisten.
- [ ] Audit sembilan varian program, PalFX, masking, premultiplication, alpha
  multiplier, shader compile/link log dan location -1/attribute optimization.
- [ ] Port juga `gldetect.c`: shader kalibrasi embedded, primitives, draw setup
  dan readback tidak boleh tertinggal pada API/immediate-mode desktop.

### V4 — Blending, target dan fitur pelengkap

- [ ] Hilangkan semantic no-op `MugenVita_BlendColor` pada jalur baru. Turunkan
  persamaan dari shader dan blend state engine; jangan mengasumsikan RGB dan
  alpha selalu mempunyai faktor sama atau semua operasi dapat menjadi additive.
- [ ] Jika constant-destination-alpha memerlukan komposit dua pass, gunakan
  intermediate target/ping-pong. Jangan membaca texture yang sedang menjadi
  attachment output draw yang sama. Ukur biaya dan uji hasil terhadap desktop.
- [ ] Pertahankan projective sampling Q untuk stretched/tinted quad.
- [ ] Implementasikan atau nyatakan unsupported logic/XOR operations dengan
  fallback yang benar; fungsi kosong tidak diterima sebagai paritas rendering.
- [ ] Selesaikan render target, viewport/scissor, orientasi texture/FBO, depth,
  capture/screenshot, font, primitives dan final screen quad.
- [ ] OpenGLScreen Vita memakai jalur upload layar tersendiri; jangan melakukan
  upscale CPU panel lalu upscale GPU lagi. System SDL2 menggunakan presenter
  yang sesuai provider; scaler SDL1 Vita tetap terbatas pada profil SDL1.

### V5 — Integrasi dan gate perangkat

- [ ] `RenderMode=OpenGL` dengan strict ON harus melaporkan renderer VitaGL
  programmable, draw shader yang benar dan pixel readback yang lulus; fallback
  System menyebabkan test gagal.
- [ ] Uji cfg asli dan generated cfg, hash shader dalam VPK, update instalasi,
  cold boot, suspend/resume, resource pressure dan keluar aplikasi.
- [ ] SDL2_ttf/scaler, RWops, audio, input dan teardown melewati gate Vita plan
  terkait. Perubahan startup/subsystem GPU dapat memengaruhi audio/input.
- [ ] Ukur CPU draw, upload, GPU/present/wait, FPS dan memory peak secara terpisah
  pada konfigurasi sama. Jangan mengubah resolusi atau mengeklaim 60 FPS tanpa data.
- [ ] Default SDL1/System tidak diganti sampai seluruh gate yang diklaim lulus.

## 12. M8 — Renderer GLES Android dan Linux ARM64

M8 adalah fitur GPU lanjutan. R05 dapat ditutup lebih dahulu melalui M5; tidak
perlu menunggu GLES untuk menghentikan silent drop true-color pada System.

1. Buat backend programmable dengan sumber API GLES dan loader yang sesuai;
   jangan mengompilasi glad desktop/fixed-function sebagai GLES.
2. Pada Android, pertahankan SDL2 native, lifecycle Android dan satu owner
   window/context. Jalur SDL_Renderer System dan direct GL tidak aktif bersamaan
   pada satu window. Jangan memakai `MUGEN_VITA_GL` untuk menghindari compile error.
   Audit guards `ANDROID_PORT` di `sprite.c`, `gfx.c`, `globals.c`, `config.c`
   dan source exclusions CMake: tabel GL yang sekarang NULL dan texture upload
   yang sengaja tidak dikompilasi harus diganti dengan dispatch provider GLES
   yang benar. Menghapus pemaksaan System saja akan memilih jalur tidak lengkap.
3. Pilih profil GLES/GLSL dari kemampuan perangkat yang diukur. Siapkan shader
   dialect desktop, GLES dan Vita secara eksplisit bila source bersama tidak
   kompatibel. Jangan menganggap string GLES 3.x menjadikan GLSL 120 valid.
4. Reuse matematika vertex, kontrak palette 2D, fixture PalFX/FBO dan compositing
   dari M7 bila cocok, tanpa memaksakan driver, shader translator atau workaround
   hardware Vita pada Android/Linux.
5. Di Linux, bedakan desktop GL compatibility dari GLES-only EGL. Pilih provider
   yang teruji, atau fallback System dengan alasan. Tidak mengklaim GPU bekerja
   hanya karena binary ARM64 berhasil link.
6. Uji context loss Android: resource CPU yang diperlukan untuk recreate harus
   tetap tersedia; object name lama dibatalkan. Renderer reset SDL_Renderer dan
   hilangnya konteks direct GL adalah lifecycle berbeda.
7. Integrasikan mode cfg sebelum pemuatan asset, capture, target FBO, resize,
   orientation/output scaling dan swap tepat sekali. Pertahankan koordinat game.

Gate: GPU sprite aktif tanpa fallback, semua fixture true-color/indexed/PalFX
lulus, resume merekonstruksi resource, dan System tetap menjadi recovery yang
teruji. M8 tidak dinyatakan selesai hanya dengan satu triangle atau layar menu.

## 13. M9 — Matriks pengujian dan bukti penerimaan

### 13.1 Test suite yang perlu tersedia

Nama test di tabel adalah **deliverable yang direncanakan**, bukan suite yang
sudah ada. Hubungkan ke CMake/Make runner sesuai platform.

| ID test | Skenario wajib | Pemeriksaan independen |
|---|---|---|
| TX01 | Resize shrink/grow/same-size/format, success dan failure setiap allocation point | Actual texture query, pointer ownership, guard/canary/ASan, gambar setelah retry |
| TX02 | Device/target reset, transient failure, teardown saat failed init | Handle tidak basi; create/destroy seimbang; recovery pixel benar |
| GL01 | OpenGLScreen repeated/duplicate resize, fullscreen, minimized, DPI | Matrix/viewport dan empat sudut hasil capture |
| PR01 | System Normal/PageFlip, 1.000 offscreen pixel updates, overlay | Satu frame final: satu upload/present; tidak ada present sebelum frame boundary |
| PR02 | Skipped frame, expose, suspend/resume, GL swap | Counter per jenis backend dan frame, tanpa present ganda |
| ABI01 | Bg legacy/neighbor sentinel 32/64-bit dan kamera fractional | Akses field semantik dan hasil posisi background |
| ABI02 | Projectile/afterimage removal dan reset loop | Tidak ada salah tipe/invalid write; pool dan frame ownership benar |
| SW01 | RAW/compressed/linked RGB24 dan RGBA32 | Expected pixel/image; tidak menerima gambar hitam sebagai lolos |
| SW02 | Alpha, add/sub, PalFX, flip, scale, shear, rotate, Q dan clipping | Golden image independen dengan toleransi per kasus yang dijelaskan |
| FT01 | Indexed/solid/blended font, scaled text, key/alpha/palette | Palettes dan alpha benar-benar dibandingkan; bukan hanya geometri |
| CF01 | Semua mode cfg, strict/fallback, unknown value, driver choice | Requested/effective renderer cocok dengan resource dan hasil draw |
| VT01 | SDL2/vitaGL context owner dan actual final link | Link map, provider log, programmable triangle dan palette readback |
| VT02 | Indexed/RGBA/FBO/blend/Q/primitives/capture pada Vita | Strict mode, pixel reference desktop, tanpa stub/fallback tersembunyi |
| ES01 | GLES Android/Linux dan context recreation | Shader aktif, readback, lifecycle dan mode fallback |
| E2E01 | Menu, select preview, stage legacy/modern, match, afterimage, screenshot, reload | Capture + log + actual renderer counters; bukan mugen.log saja |

Mutasi yang wajib membuat test gagal: menghapus identity reset; mengembalikan
UpdateRect immediate-present; menulis metadata sebelum alokasi sukses; membaca
Bg+0x64 lagi; melewati RAW32; membuang Q; no-op constant alpha; mengganti hasil
capture menjadi hitam atau menonaktifkan perbandingan palette.

### 13.2 Platform dan jenis bukti

| Target | Build dan jalur yang harus diuji | Bukti minimum |
|---|---|---|
| Win32 SDL1 MSVC | System, DirectX policy, OpenGLScreen, OpenGL | Fidelity terhadap original, screenshot, resize; CMake preset referensi |
| Win32 MinGW SDL1 | Mode yang dibangun profil referensi | Build/link SDL1, differential yang relevan |
| Win32 MinGW SDL2 | System Normal/PageFlip, OpenGLScreen, OpenGL | Failure injection, actual driver, capture dan counter |
| Win64 MinGW SDL2 | Seluruh mode desktop yang tersedia | Compiler x64 sebenarnya, LLP64/layout, scenario projectile/afterimage |
| Linux x64 SDL2 | Software/dummy untuk logic; desktop GL untuk visual | LP64, sanitizer dan GL pada display nyata/headless GL yang terverifikasi |
| Linux ARM64 SDL2 | System; GL atau GLES sesuai provider perangkat | Binary ARM64 dijalankan; driver/GPU tercatat, bukan emulasi x64 |
| Android ARM64 | System native SDL2; GLES hanya setelah M8 | Host + APK perangkat, pause/resume, output change dan GPU reset bila tersedia |
| Vita ARM32 | SDL1 recovery; SDL2 System dan VitaGL setelah implementasi | Cross-build, final VPK, hardware cold boot, suspend/resume dan pixel capture |

Driver dummy baik untuk ownership dan present-count, tetapi tidak membuktikan
shader, vsync, GL capability, shader translator Vita, atau kualitas visual GPU.
Emulator tidak menggantikan seluruh gate hardware. Sanitizer yang tidak tersedia
di suatu target diganti dengan guard/canary/telemetry dan test perangkat yang
sesuai; jangan melaporkan sanitizer target tersebut lulus.

### 13.3 Artefak bukti

Simpan hasil eksekusi sementara di `build/render-validation/<sha>/<target>/<mode>/`:

- metadata build/dependency/driver, requested/effective mode dan strict status;
- cfg fixture dan ukuran game/buffer/window/drawable yang dipakai;
- counter, log, exit status, failure injection point, sanitizer/canary report;
- raw capture beserta dimensions, pitch, masks, palette, key dan alpha state;
- expected image, pixel diff, area toleransi dan penjelasan jika tidak exact;
- measurement CPU/GPU/present serta memory peak dengan prosedur yang sama.

Untuk bukti rilis, commit ringkasan kecil yang menunjuk SHA dan checksum artefak
retained melalui mekanisme proyek/CI. Jangan menumpuk binary atau data game
berlisensi dalam repository. Jangan mengubah normalisasi log agar gambar salah
atau fallback GPU terlihat sebagai sukses.

### 13.4 Kriteria selesai lintas platform

- [ ] R01–R04 tertutup oleh regression test yang fail pada implementasi lama.
- [ ] R05 mempunyai capability matrix dan seluruh fitur yang diiklankan lulus;
  kemampuan yang tersisa belum didukung ditulis spesifik dan tidak silent-drop.
- [ ] SDL1 reference tidak mengalami perubahan fidelity yang tidak dijelaskan.
- [ ] Build Windows 32/64, Linux x64/ARM64, Android dan Vita tidak saling membawa
  macro, header, library atau resource ownership platform lain.
- [ ] Semua requested mode yang diklaim didukung mempunyai effective mode sama.
- [ ] GPU Vita/GLES tidak diberi status selesai berdasarkan System fallback.
- [ ] Tidak ada perubahan default backend/resolusi yang dipakai untuk menyembunyikan kegagalan.
- [ ] Dokumentasi menampilkan nama binary, path build dan status yang sesuai artefak final.

## 14. Pembagian commit/PR dan rollback

| Perubahan | Isi yang boleh digabung | Verifikasi sebelum merge |
|---|---|---|
| P0 baseline | Fixture, counter test-only, expected results | Test baru mendeteksi kondisi lama dengan alasan yang benar |
| P1 safety | M1 atomic resize dan error handling | TX01/TX02, ASan/guard, recovery |
| P2 resize GL | M2 projection/viewport | GL01, screenshot, repeated resize |
| P3 presentation | M3 frame boundary | PR01/PR02 dan Android regression |
| P4 ABI | M4 typed access/lifecycle | ABI01/ABI02, reference32 dan native64 |
| P5 true-color | M5 dalam subcommit opaque, alpha, transform/PalFX | SW01/SW02, indexed/font nonregression |
| P6 capability | M6 cfg/log/strict/fallback | CF01 dan negative init tests |
| P7 Vita platform | V1–V2 dependency/provider/context | CMake combinations, map, hardware probe |
| P8 Vita drawing | V3–V4 shader/geometry/compositing/targets | VT02 per fitur; tanpa perubahan default |
| P9 Vita qualification | V5 dan update status M0–M9 Vita terkait | Device evidence dan rollback profile |
| P10 GLES | M8 bertahap per platform | ES01, lifecycle dan System recovery |
| P11 release | M9 docs, CI dan keputusan default | Seluruh gate untuk fitur yang diklaim |

Rollback:

1. Setiap commit koreksi dapat direvert tanpa mengembalikan artefak game atau
   mengubah SDL dependency platform lain. Jangan menyatukan refactor besar,
   optimisasi, dan perubahan default dalam commit yang sama.
2. Simpan profil SDL1/Win32 referensi dan Vita SDL1/System pada build directory
   berbeda. Nama output menyatakan toolchain, target dan backend yang sebenarnya.
3. Perluasan software/GPU baru mempunyai selector internal/build profile yang
   jelas selama rollout. Jangan fallback diam-diam dalam strict qualification.
4. Bila suatu GPU provider gagal, kembali ke System hanya setelah resource
   owner lama dilepas. Jika tidak dapat menjamin teardown, hentikan init dengan
   pesan yang berguna, bukan melanjutkan memakai handle campuran.
5. Setelah regresi, tandai gate yang kembali OPEN serta simpan repro dan SHA;
   jangan menghapus test, mengganti golden menjadi gambar rusak, atau memperlebar
   toleransi tanpa alasan visual dan teknis yang dapat direview.

## 15. Risiko dan keputusan yang masih harus dibuktikan

| Risiko/keputusan | Penanganan |
|---|---|
| Koreksi shared code mengubah quirk executable asli | M0 differential; reference profile dipertahankan dan perbedaan ditandai |
| Alokasi texture+surface sementara menaikkan peak memory | Hitung old+candidate+scratch, uji pressure; utamakan keselamatan dan retry dibanding state campuran |
| Per-frame flush menghilangkan update di luar loop | Audit semua caller UpdateRect/Flip; immediate diagnostics menjadi API eksplisit |
| True-color software lebih mahal pada Vita | Profilkan setelah benar; cache decode/scratch; tidak mengubah resolusi atau membuang blend |
| Makna afterimage reset masih salah tipe pada baseline | Telusuri helper canonical dan reference behavior sebelum edit; angka offset bukan satu-satunya masalah |
| Provider SDL2-vitaGL belum terbukti cocok | Gate hardware V1/V2; tidak ada dual GXM owner, dependency dipin dan final link diaudit |
| Shader diterjemahkan tetapi custom program tidak aktif | Programmable vertex submission, program/draw counters dan readback independen |
| Constant-alpha/Q dianggap detail kosmetik | Keduanya menjadi gate pixel parity; no-op tidak diterima |
| Toleransi GPU menutupi kesalahan besar | Batas per kasus dari pengukuran; palette/alpha/image content selalu dibandingkan |
| Ketersediaan toolchain/perangkat | Status build-only, host-tested dan device-tested dipisah; tidak mengisi PASS tanpa hasil |

## 16. Contoh menjalankan baseline dan qualification

Perintah di bawah dijalankan dari root repository pada environment/toolchain
target yang sesuai. Ini resep eksekusi, bukan laporan bahwa build sudah
dijalankan saat menulis plan. Jangan menjalankan dua build yang menulis subtree
Android/Gradle sama secara bersamaan.

### 16.1 Desktop yang sudah tersedia

MSVC Win32, dari environment Windows yang sesuai:

```bat
tools\msvc_env.bat cmake --preset x86-release
tools\msvc_env.bat cmake --build --preset x86-release
```

MinGW Win32, setelah memilih compiler dan static dependency kit i686:

```sh
make MUGEN_TARGET=win32 SDL_BACKEND=1 BUILD=build/render-win32-sdl1 check-toolchain
make MUGEN_TARGET=win32 SDL_BACKEND=1 BUILD=build/render-win32-sdl1 -j8
make MUGEN_TARGET=win32 SDL_BACKEND=2 BUILD=build/render-win32-sdl2 check-toolchain
make MUGEN_TARGET=win32 SDL_BACKEND=2 BUILD=build/render-win32-sdl2 -j8
```

MinGW Win64 memerlukan environment x86_64 beserta dependency kit x64 sendiri:

```sh
make MUGEN_TARGET=win64 SDL_BACKEND=2 BUILD=build/render-win64-sdl2 check-toolchain
make MUGEN_TARGET=win64 SDL_BACKEND=2 BUILD=build/render-win64-sdl2 -j8
```

Linux x64, dengan paket development SDL2/companions dan compiler native:

```sh
make MUGEN_TARGET=linux-x64 SDL_BACKEND=2 BUILD=build/render-linux-x64 check-toolchain
make MUGEN_TARGET=linux-x64 SDL_BACKEND=2 BUILD=build/render-linux-x64 -j8
```

Pada mesin Linux ARM64, gunakan `MUGEN_TARGET=linux-arm64` dan build directory
tersendiri. Untuk cross-build, tetapkan compiler, sysroot, AR/STRIP dan pkg-config
target secara konsisten; jangan menautkan library host x64 ke binary ARM64.

Gunakan salinan data game di bawah `build/render-fixtures/` untuk skenario
runtime. Sebelum tiap run, salin cfg fixture yang sesuai, hapus log run sebelumnya
di fixture sendiri, catat hash cfg, dan rekam nama executable hasil build aktual.
Jangan memakai `make install` ke tree referensi selama eksperimen ini.

### 16.2 Host regression Android yang sudah tersedia

Memerlukan CMake dan header/library SDL2, SDL2_mixer, SDL2_ttf serta dependensi
development yang diminta `port/android/tests/CMakeLists.txt`:

```sh
cmake -S port/android/tests -B build/render-android-host
cmake --build build/render-android-host
ctest --test-dir build/render-android-host --output-on-failure
```

Test CMake tersebut memasang driver dummy untuk kasus host yang relevan.
Ketersediaan font memengaruhi coverage TTF; catat bila kasus font di-skip.
Setelah M0 menambah suite TX/GL/PR/ABI/SW/CF, daftarkan target/test secara eksplisit
dan tulis perintah yang nyata di README test. Jangan mencantumkan nama executable
usulan dalam bukti seolah-olah sudah dapat dijalankan.

Untuk APK, gunakan `port/android/build.sh --help` dan workflow SDK/NDK yang ada.
Build memakai game data fixture atau skenario provisioning yang sudah diuji;
`--no-assets` tidak membuktikan first-run game lengkap. Uji perangkat APK tetap
terpisah dari `ctest` host.

### 16.3 Vita baseline dan profil mendatang

Contoh baseline SDL1/System yang tersedia saat ini, setelah VitaSDK/dependensi
disiapkan dan salinan data game tersedia:

```sh
cmake -S port/psvita -B build/render-vita-sdl1 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DMUGEN_VITA_DATA_DIR="$PWD/build/render-fixtures/game" \
  -DMUGEN_VITA_RENDERMODE=system
cmake --build build/render-vita-sdl1
```

Periksa `mugen-psvita.self` dan `mugen-psvita.vpk` pada output tersebut. Lakukan
instalasi hardware melalui workflow Vita yang sudah ada; jangan menganggap
selesainya packaging sebagai keberhasilan startup/rendering.

Perintah profil SDL2/OpenGL strict baru ditambahkan sesudah V1 mengimplementasikan
opsi yang direncanakan. Gunakan build directory berbeda dan dokumentasikan
provider yang benar-benar dipilih. Menambahkan `-DMUGEN_VITA_SDL_BACKEND=2` pada
baseline yang belum membaca opsi tersebut tidak menjadikannya build SDL2.

## 17. Referensi sumber dan kontrak API

Baseline repository:

- [Commit yang diperiksa](https://github.com/leonkasovan/mugen-unleashed/tree/81833c83ab99fad5b66bdde273242407c23006ea).
- [SDL2 video dan resource](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/src/sdl2compat.c).
- [Video mode/resize](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/src/video.c),
  [frame](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/src/frame.c),
  [presenter](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/src/gfx.c).
- [Sprite dispatch](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/src/sprite.c),
  [kamera](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/src/camera.c),
  [GL renderer](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/src/glrender.c).
- [Presenter native Android](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/port/android/app/src/main/jni/src/android_video.c).
- [Vita capability shim](https://github.com/leonkasovan/mugen-unleashed/blob/81833c83ab99fad5b66bdde273242407c23006ea/port/psvita/src/vita_gl_shim.c),
  [pin vitaGL](third_party/vitagl/VENDORED.md): `9bf3e3f4e55efd1f4a191b179144f7f9a98cd925`.

Dokumentasi resmi SDL2, diperiksa 4 November 2026:

- [SDL_UpdateTexture](https://wiki.libsdl.org/SDL2/SDL_UpdateTexture): rect NULL
  mengunggah seluruh texture; source format dan pitch harus benar.
- [SDL_QueryTexture](https://wiki.libsdl.org/SDL2/SDL_QueryTexture): pemeriksaan
  format, access, width dan height texture aktual pada test.
- [SDL_RenderPresent](https://wiki.libsdl.org/SDL2/SDL_RenderPresent): compose
  frame sebelum present, main-thread ownership, backbuffer tidak dipertahankan.
- [SDL_GL_GetDrawableSize](https://wiki.libsdl.org/SDL2/SDL_GL_GetDrawableSize):
  ukuran drawable pixel untuk viewport dapat berbeda dari window logical size.
- [SDL_RenderSetLogicalSize](https://wiki.libsdl.org/SDL2/SDL_RenderSetLogicalSize):
  logical resolution terpisah dari output, termasuk scaling input mouse/touch.
- [SDL_HINT_VIDEODRIVER](https://wiki.libsdl.org/SDL2/SDL_HINT_VIDEODRIVER): pemilihan
  driver dilakukan sebelum SDL_Init; bukan pemilihan SDL_Renderer Direct3D.
- [SDL_RendererFlags](https://wiki.libsdl.org/SDL2/SDL_RendererFlags): akselerasi,
  present-vsync dan dukungan target texture merupakan capability yang berbeda.

Referensi kontrak API tidak merupakan bukti hasil runtime proyek. Setiap status
selesai tetap membutuhkan test, SHA implementasi, dan hasil platform yang sesuai.

### M0 test matrix (normative)

Fixture SDL di bawah dipakai oleh TX01/TX02/GL01/PR01/PR02/ABI01/ABI02/SW01/
SW02/FT01/CF01. Setiap ukuran adalah byte-exact; tidak ada gambar "cukup mirip".
Kolom Backend menyatakan di mana case berjalan; di luar itu case di-skip dengan
alasan yang dicatat.

| ID | Fixture | Ukuran | Backend | Expected | Menutup |
|---|---|---|---|---|---|
| F-RAMP565 | Ramp RGB565 0..255 per channel + pola per-baris | 64x64 | SDL1, SDL2 | .raw identik SDL1 vs SDL2 | TX01, §6.1 pitch |
| F-IDX8 | indexed8 4x4, palette nontrivial (hitam, merah, hijau, biru, alpha 128) | 4x4 | SDL1, SDL2 | .raw palette-exact | M0, SW01 |
| F-PNGRGB | PNG RGB 2x2 (decoder real) | 2x2 | SDL2 shim | Pixel match decode | SW01, M5 |
| F-PNGRGBA | PNG RGBA 2x2, alpha 0/128/255 | 2x2 | SDL2 shim | Mask+blend exact | SW01/SW02 |
| F-QUAD | Quad berkoordinat (sudut + tengah terlabel) | 320x240 | semua | Posisi terhitung independen | GL01 |
| F-CORNER | Penanda empat sudut 8x8 di atas checkerboard | 64x64 | GL modes | Empat sudut sejajar | GL01 |
| F-FONTKEY | solid + blended font "Ag", scale 1x dan 2x | text | SDL1, SDL2 | Key/alpha/palette dibanding | FT01 |
| F-PADX | Surface 5x3 RGB565 (pitch berpading) | 5x3 | SDL1, SDL2 | Row benar | §6.1 |
| F-ABI | Bg legacy=0/1 vs tetangga sentinel + kamera fractional | synth | host | Cabang benar | ABI01 |
| F-PROJ | Afterimage create/remove/reset loop 100x | synth | host | Ownership benar | ABI02 |

Counter diagnostik (draw, upload, present, swap, resource create/destroy,
fallback) diimplementasikan sebagai wrapper test-only di `Sdl2_Present` dan
`SDL_UpdateTexture`/`SDL_CreateTexture` — bukan counter di produksi. Test
memakai `SDL_QueryTexture` sebagai pemeriksaan independen, tidak pernah metadata
yang baru ditulis. Mutasi wajib-gagal (§13.1) diverifikasi di CI M9.
