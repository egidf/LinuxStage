# Product Requirements Document (PRD)
## Desktop Audio Player dengan Rack Efek LSP Plugins

**Status:** Draft v1.0  
**Tanggal:** 12 Oktober 2026  
**Platform target:** Linux dan Windows  
**Jenis produk:** Aplikasi desktop pemutar audio dengan pemrosesan efek real-time  
**Nama kerja:** LSP Audio Player

---

## 1. Ringkasan Produk

LSP Audio Player adalah aplikasi desktop untuk memutar audio dengan dua jalur pemutaran independen: **Main Player** dan **BGM Player** (background music). Setiap jalur memiliki playlist, kontrol transport, pengaturan level, dan rack efek sendiri. Pengguna dapat menambahkan, menghapus, serta mengurutkan plugin efek dalam rack.

Pada Linux, versi awal memprioritaskan hosting plugin **LV2** dengan katalog plugin melalui **Lilv** dan dukungan antarmuka plugin melalui **Suil**. Pada Windows, hosting plugin dirancang melalui adapter **VST3**, dengan **JUCE** sebagai kandidat teknologi implementasi. Antarmuka plugin asli (*native plugin UI*) digunakan jika kompatibel dan dapat ditanamkan dengan aman; jika tidak, aplikasi dapat membuka editor sebagai jendela terpisah atau menyediakan tampilan parameter generik.

Dokumen ini mendefinisikan kebutuhan produk dan batasan implementasi. Dokumen ini bukan klaim bahwa semua fitur sudah diimplementasikan.

## 2. Masalah yang Ingin Diselesaikan

Pengguna audio membutuhkan pemutar audio sederhana yang dapat menjalankan musik utama dan musik latar secara terpisah, sambil menerapkan efek sesuai urutan yang diinginkan. Banyak pemutar biasa tidak menyediakan rack efek plugin yang fleksibel atau pengalaman kerja yang berorientasi pada produksi audio.

Produk ini bertujuan menyediakan alur kerja yang mudah dipahami tanpa memerlukan pengguna membuka DAW penuh hanya untuk pemutaran audio dan efek.

## 3. Tujuan Produk

1. Memutar berkas audio melalui dua player independen.
2. Menyediakan rack efek terpisah untuk Main Player dan BGM Player.
3. Mendukung penemuan dan pemuatan plugin dengan format yang sesuai platform.
4. Menjaga urutan efek sesuai susunan rack pengguna.
5. Menyediakan kontrol fade-in dan fade-out untuk pemutaran.
6. Menyimpan playlist, konfigurasi rack, serta preferensi pengguna.
7. Menyediakan fondasi lintas platform dengan pemisahan backend plugin.
8. Mengutamakan stabilitas audio dan perilaku yang dapat diprediksi.

## 4. Bukan Tujuan (Non-goals)

Versi awal tidak ditujukan untuk:
- Menggantikan DAW lengkap.
- Merekam multitrack atau mengedit waveform secara mendalam.
- Menjadi host untuk seluruh format plugin sekaligus pada semua platform.
- Menjamin setiap UI plugin pihak ketiga dapat ditanamkan di dalam aplikasi.
- Menyediakan sinkronisasi cloud atau kolaborasi daring.
- Menjanjikan pemutaran tanpa jeda (*gapless*) untuk semua format dan semua kombinasi plugin sebelum diuji.

## 5. Pengguna Sasaran

- Musisi dan produser rumahan yang memerlukan pemutar dengan efek.
- Pengguna yang menjalankan musik utama dan musik latar secara bersamaan.
- Pengguna Linux yang menggunakan plugin LV2, termasuk plugin LSP.
- Pengguna Windows yang memakai plugin VST3.
- Pengguna yang menginginkan antarmuka sederhana tanpa workflow DAW penuh.

## 6. Prinsip Desain

- **Jelas:** status player, playlist, rack, dan audio mudah dipahami.
- **Terpisah:** Main Player dan BGM Player memiliki kontrol serta rack masing-masing.
- **Tidak merusak:** kegagalan satu plugin sebisa mungkin tidak menyebabkan seluruh aplikasi berhenti.
- **Lintas platform:** logika player dan rack dipisahkan dari implementasi format plugin.
- **Jujur terhadap kompatibilitas:** UI native hanya ditawarkan jika plugin dan platform mendukungnya.
- **Aman untuk audio real-time:** callback audio tidak boleh melakukan operasi yang dapat memblokir atau mengalokasikan memori secara tidak terkontrol.

## 7. Ruang Lingkup MVP

### 7.1 Termasuk dalam MVP

- Aplikasi desktop Linux dengan Qt Widgets.
- Pemutaran berkas audio umum, dengan format awal ditetapkan saat implementasi backend audio.
- Dua player: Main Player dan BGM Player.
- Playlist per player.
- Play, pause, stop, next, previous, dan seek jika format/backend mendukungnya.
- Kontrol volume per player.
- Fade-in dan fade-out dengan durasi yang dapat diatur.
- Rack efek terpisah per player.
- Menemukan plugin LV2 yang terpasang di sistem Linux.
- Menampilkan nama, URI/ID, format, dan ketersediaan UI plugin.
- Menambah, menghapus, dan mengurutkan plugin pada rack.
- Membuka editor plugin asli ketika kompatibel.
- Menyimpan dan memuat sesi/configuration lokal.
- Pesan kesalahan yang dapat dipahami pengguna.
- Logging untuk diagnosis.

### 7.2 Setelah MVP

- Build Windows dengan hosting VST3.
- Pengaturan routing audio yang lebih lengkap.
- Preset rack dan preset sesi.
- Pengelolaan plugin favorit dan pencarian katalog.
- Tampilan meter level dan clipping.
- Crossfade antar-track.
- Gapless playback setelah backend dan format teruji.
- Ekspor/impor konfigurasi rack.
- Tema visual tambahan.

## 8. Arsitektur Produk

### 8.1 Lapisan utama

1. **UI Layer**  
   Antarmuka player, playlist, rack, dialog pengaturan, dan jendela editor plugin.

2. **Application/Core Layer**  
   Mengatur transport, playlist, fade envelope, status player, sesi, serta perintah rack.

3. **Audio Engine**  
   Membaca/men-decode audio, mengelola buffer, menjalankan rantai DSP, mencampur output Main Player dan BGM Player, lalu mengirim hasil ke perangkat audio.

4. **Plugin Host Layer**  
   Antarmuka umum untuk penemuan, instansiasi, pemrosesan, parameter, dan editor plugin.

5. **Platform Adapter Layer**  
   - Linux: LV2 melalui Lilv; UI plugin melalui Suil bila kompatibel.
   - Windows: VST3 melalui adapter host yang dipilih/diintegrasikan; JUCE merupakan kandidat untuk implementasi.
   - Adapter tambahan dapat ditambahkan tanpa mengubah logika rack inti.

### 8.2 Diagram konseptual

```text
Main Player -> Decoder -> Main FX Rack ----\
                                            -> Master Mixer -> Audio Device
BGM Player  -> Decoder -> BGM FX Rack -----/

UI / Playlist / Transport / Rack Controller
                    |
               Audio Engine
                    |
              Plugin Host API
             /               \
       Linux LV2           Windows VST3
       Lilv + Suil         Adapter host
```

### 8.3 Prinsip pemrosesan audio

- Setiap player memiliki rantai DSP tersendiri.
- Urutan plugin pada rack menentukan urutan pemrosesan sinyal.
- Fade dan volume diterapkan pada titik yang ditentukan secara konsisten dan didokumentasikan.
- Main Player dan BGM Player dicampur ke master output.
- Jika plugin gagal dimuat, aplikasi menampilkan status gagal dan tidak diam-diam mengubah konfigurasi tanpa memberi tahu pengguna.
- Perubahan struktur rack sebaiknya dilakukan di luar callback audio, kemudian diterapkan melalui mekanisme aman seperti command queue atau pertukaran graph yang tersinkronisasi.
- Callback audio tidak boleh melakukan I/O disk, membuka dialog, menunggu mutex yang berpotensi memblokir, atau membuat alokasi memori tak terduga.

## 9. Kebutuhan Fungsional

Prioritas: **P0** = wajib untuk MVP; **P1** = penting setelah fondasi stabil; **P2** = pengembangan lanjutan.

### FR-01 — Main Player (P0)

**Deskripsi:** Player utama untuk memutar daftar audio.

**Kebutuhan:**
- Pengguna dapat menambahkan satu atau beberapa berkas ke playlist.
- Pengguna dapat memilih item untuk diputar.
- Pengguna dapat play, pause, stop, next, dan previous.
- Pengguna dapat melihat nama berkas dan status pemutaran.
- Pengguna dapat mengatur volume.
- Pengguna dapat menghapus item dari playlist.
- Playlist tetap terpisah dari playlist BGM.

**Kriteria penerimaan:**
- Perintah transport mengubah status player secara benar.
- Playlist tetap tampil setelah sesi disimpan dan dibuka kembali.
- Berkas yang tidak dapat dibaca menghasilkan pesan yang jelas.

### FR-02 — BGM Player (P0)

**Deskripsi:** Player khusus musik latar yang berjalan independen dari Main Player.

**Kebutuhan:**
- Memiliki playlist, transport, volume, dan status sendiri.
- Dapat mulai atau berhenti tanpa mengubah status Main Player.
- Memiliki rack efek sendiri.
- Dapat dimute tanpa mematikan Main Player.

**Kriteria penerimaan:**
- Play/pause/stop BGM tidak mengubah posisi atau status Main Player.
- Pengaturan volume dan rack BGM tidak memengaruhi Main Player.

### FR-03 — Fade-in dan Fade-out (P0)

**Deskripsi:** Perubahan level secara bertahap untuk menghindari awal/akhir audio yang mendadak.

**Kebutuhan:**
- Pengguna dapat mengatur durasi fade-in dan fade-out.
- Fade dapat diterapkan pada Main Player dan BGM Player secara independen.
- Stop atau perpindahan track harus mengikuti perilaku fade yang didefinisikan dalam pengaturan.
- Fade menggunakan envelope yang halus dan tidak menimbulkan klik yang terdengar.

**Kriteria penerimaan:**
- Fade dapat diuji pada beberapa durasi.
- Tidak ada lonjakan level mendadak saat fade dimulai atau selesai.
- Durasi dan perilaku fade konsisten pada sample rate yang didukung.

### FR-04 — Plugin Catalog (P0)

**Deskripsi:** Menemukan plugin yang tersedia pada sistem.

**Linux:**
- Memindai plugin LV2 menggunakan Lilv.
- Menampilkan nama plugin, URI, dan ketersediaan UI yang terdeteksi.
- Menangani metadata yang tidak lengkap.

**Windows:**
- Katalog VST3 ditambahkan pada milestone Windows.

**Kriteria penerimaan:**
- Aplikasi dapat memindai plugin tanpa crash pada metadata yang valid.
- Plugin dengan metadata atau binary bermasalah ditandai/logged tanpa menggagalkan seluruh pemindaian.
- Pengguna dapat menyegarkan daftar plugin.

### FR-05 — Rack Efek (P0)

**Deskripsi:** Daftar plugin efek yang diproses secara berurutan.

**Kebutuhan:**
- Setiap player memiliki rack terpisah.
- Pengguna dapat menambah dan menghapus plugin.
- Pengguna dapat memindahkan plugin naik/turun atau melalui drag-and-drop jika tersedia.
- Urutan rack disimpan dalam sesi.
- Pengguna dapat melihat nama dan status setiap plugin.
- Plugin yang gagal dimuat menampilkan status dan informasi kesalahan.

**Kriteria penerimaan:**
- Urutan tampilan rack sama dengan urutan DSP.
- Menambah/menghapus/memindahkan plugin pada satu rack tidak mengubah rack player lain.
- Urutan tetap sama setelah aplikasi dimulai ulang.

### FR-06 — Native Plugin UI (P0 untuk Linux spike; dukungan kompatibilitas bersyarat)

**Deskripsi:** Membuka editor yang disediakan plugin.

**Kebutuhan:**
- Linux menggunakan Suil untuk UI LV2 yang kompatibel dengan host dan toolkit aplikasi.
- Aplikasi memeriksa apakah plugin menyediakan UI yang dapat digunakan.
- Jika UI tidak dapat ditanamkan, aplikasi boleh membuka jendela terpisah atau menampilkan parameter generik.
- Status parameter UI dan DSP harus disinkronkan.
- Menutup editor tidak boleh otomatis menghapus plugin dari rack.

**Kriteria penerimaan:**
- Minimal satu plugin LV2 yang diketahui kompatibel berhasil diinstansiasi dan editor-nya dibuka.
- UI yang tidak kompatibel menghasilkan fallback atau pesan jelas, bukan crash.
- Membuka dan menutup editor berulang kali tidak menyebabkan resource leak yang diketahui.

**Catatan:** Ketersediaan UI native bergantung pada plugin, toolkit UI, host, dan platform. Dukungan universal tidak dijanjikan.

### FR-07 — Audio Engine dan Master Mixer (P0)

**Kebutuhan:**
- Mencampur output Main Player dan BGM Player.
- Mengelola sample rate dan buffer audio yang didukung backend.
- Menyediakan master volume.
- Mencegah clipping sejauh mungkin melalui meter dan/atau peringatan; limiter otomatis bukan syarat MVP.
- Menangani perangkat audio yang terputus dengan pesan dan pemulihan yang aman jika memungkinkan.

**Kriteria penerimaan:**
- Kedua player dapat diputar bersamaan.
- Perubahan volume satu player tidak mengubah level player lain.
- Perangkat audio yang tidak tersedia menghasilkan pesan yang jelas.

### FR-08 — Penyimpanan Sesi (P0)

**Deskripsi:** Menyimpan status konfigurasi untuk digunakan kembali.

**Data yang disimpan:**
- Playlist dan lokasi berkas.
- Urutan plugin setiap rack.
- Format dan identifier plugin.
- Parameter/preset plugin jika didukung host dan plugin.
- Volume, fade, master setting, serta preferensi relevan.
- Versi format sesi.

**Kriteria penerimaan:**
- Sesi yang valid dapat disimpan dan dibuka kembali.
- Berkas yang hilang ditandai sebagai missing, bukan dihapus diam-diam.
- Sesi versi lebih baru atau rusak ditangani dengan pesan yang jelas.

### FR-09 — Logging dan Diagnostik (P0)

**Kebutuhan:**
- Mencatat kegagalan pemindaian, pemuatan, instansiasi, editor UI, dan perangkat audio.
- Menyediakan opsi membuka atau menyalin log.
- Tidak mencatat informasi sensitif yang tidak diperlukan.

### FR-10 — Preset Rack (P1)

- Pengguna dapat menyimpan dan memuat susunan rack sebagai preset.
- Preset mencatat identifier plugin, urutan, dan parameter jika didukung.
- Preset yang merujuk plugin yang tidak tersedia tetap dapat dibuka dengan item bertanda missing.

### FR-11 — Meter Audio (P1)

- Meter untuk Main Player, BGM Player, dan master output.
- Peak hold atau indikator clipping dapat ditambahkan.
- Meter tidak boleh membebani callback audio atau UI secara berlebihan.

### FR-12 — Windows VST3 (P1)

- Menemukan plugin VST3 di lokasi standar dan lokasi yang dikonfigurasi.
- Memuat, memproses, mengatur parameter, dan membuka editor VST3.
- Menyimpan identifier dan parameter plugin dalam format sesi bersama.
- Memisahkan kode spesifik VST3 dari logika player/rack.

## 10. Kebutuhan Antarmuka (UI/UX)

### 10.1 Struktur jendela

- **Header/menu:** File, Playback, View, Settings, Help.
- **Area Main Player:** nama track, progress, transport, volume, fade.
- **Area BGM Player:** kontrol serupa dengan Main Player.
- **Rack Main Player:** daftar efek horizontal atau vertikal sesuai keputusan desain final.
- **Rack BGM Player:** daftar efek independen.
- **Master section:** master volume, output device, dan meter jika tersedia.
- **Plugin browser:** pencarian, kategori/format, nama plugin, status UI.
- **Plugin editor:** UI asli jika tersedia, fallback bila tidak.

### 10.2 Prinsip layout

- Main Player dan BGM Player harus mudah dibedakan.
- Status playing, paused, stopped, missing, dan plugin error tidak hanya dibedakan dengan warna; gunakan label atau ikon.
- UI tetap dapat digunakan pada resolusi desktop umum dan saat jendela di-resize.
- Editor plugin tidak boleh menutupi atau merusak status rack.
- Jangan menganggap semua UI plugin memiliki ukuran tetap.

### 10.3 Aksesibilitas dan kemudahan penggunaan

- Kontrol utama dapat diakses dengan keyboard.
- Label kontrol jelas.
- Pesan kesalahan menyebut masalah dan tindakan yang dapat dicoba.
- Jangan menggunakan warna sebagai satu-satunya pembeda status.

## 11. Kebutuhan Nonfungsional

### NFR-01 — Stabilitas
- Crash plugin yang tidak dapat diisolasi harus diminimalkan melalui validasi dan pengelolaan lifecycle yang ketat.
- Kegagalan satu plugin harus dilaporkan secara eksplisit.
- Uji buka/tutup editor dan tambah/hapus plugin berulang kali.

### NFR-02 — Performa
- Pemrosesan audio harus berjalan pada callback real-time dengan pekerjaan yang terbatas dan dapat diprediksi.
- Operasi disk, scan plugin, dan operasi UI tidak boleh berjalan di callback audio.
- Target latency dan penggunaan CPU ditetapkan setelah prototipe mengukur backend dan perangkat uji.

### NFR-03 — Portabilitas
- Logika playlist, fade, rack, sesi, dan mixer tidak boleh bergantung pada satu format plugin.
- Adapter plugin dan backend audio harus memiliki antarmuka yang jelas.

### NFR-04 — Pemeliharaan
- Kode dipisahkan menjadi modul player, engine, rack, plugin host, persistence, dan UI.
- Pengujian otomatis digunakan untuk logika yang tidak membutuhkan perangkat audio fisik.
- Dokumentasi build tersedia untuk Linux dan Windows.

### NFR-05 — Keamanan dan ketahanan
- Plugin pihak ketiga diperlakukan sebagai kode eksternal yang berpotensi gagal.
- Pemindaian plugin tidak boleh menganggap semua metadata dapat dipercaya.
- Jangan memuat plugin dari lokasi yang tidak dikenal secara diam-diam tanpa konfigurasi pengguna.

## 12. Model Data Konseptual

Contoh struktur logis (bukan API final):

```cpp
enum class RackTarget {
    MainPlayer,
    BgmPlayer
};

struct PluginKey {
    PluginFormat format;  // LV2, VST3, dan lainnya jika ditambahkan
    std::string id;       // URI LV2 atau identifier VST3
};

struct RackEntry {
    PluginKey plugin;
    bool enabled;
    std::string displayName;
    PluginLoadStatus status;
};

struct PlayerConfig {
    std::vector<std::string> playlistPaths;
    float volume;
    FadeConfig fade;
    std::vector<RackEntry> rack;
};

struct SessionConfig {
    int schemaVersion;
    PlayerConfig mainPlayer;
    PlayerConfig bgmPlayer;
    float masterVolume;
    std::string outputDeviceId;
};
```

Tipe final, kepemilikan objek, thread safety, dan serialisasi ditentukan dalam desain teknis.

## 13. Teknologi dan Keputusan Implementasi

### Linux — prototipe awal

- **Bahasa:** C++17 atau standar lebih baru yang dipilih proyek.
- **UI awal:** Qt 6 Widgets.
- **LV2 discovery/host integration:** Lilv.
- **LV2 UI integration:** Suil untuk UI yang kompatibel.
- **Build:** CMake + Ninja.
- **Audio backend:** dipilih dan divalidasi pada spike teknis; keputusan harus mempertimbangkan ketersediaan perangkat, latency, dan dukungan distro.

### Windows — target lintas platform

- **Plugin format:** VST3.
- **Framework kandidat:** JUCE untuk implementasi adapter/host.
- **UI:** Qt dapat dipertahankan untuk konsistensi UI utama; integrasi editor plugin harus diuji secara khusus.
- **Audio backend:** dipilih berdasarkan kebutuhan perangkat dan distribusi aplikasi.

### Catatan arsitektur penting

Jangan menganggap implementasi host LV2 melalui Lilv/Suil dan host VST3 melalui JUCE dapat disatukan hanya dengan mengganti nama format. Keduanya membutuhkan adapter dan pengujian lifecycle masing-masing. Pastikan lisensi semua library, SDK, dan plugin yang didistribusikan sesuai dengan cara aplikasi dipaketkan.

## 14. Risiko dan Mitigasi

| Risiko | Dampak | Mitigasi |
|---|---|---|
| UI native plugin tidak kompatibel untuk ditanamkan | Editor tidak tampil atau crash | Uji kompatibilitas; fallback jendela terpisah atau parameter generik |
| Plugin pihak ketiga crash | Audio/app berhenti | Logging, validasi, uji lifecycle; pertimbangkan proses terpisah pada versi lanjut |
| Perubahan rack mengganggu callback audio | Glitch, crash, atau race condition | Command queue atau graph swap tersinkronisasi |
| Format sesi tidak kompatibel lintas OS | Plugin tidak ditemukan pada OS lain | Simpan format + identifier; tandai plugin missing dan pertahankan metadata |
| Latency/performa belum diketahui | Pengalaman audio buruk | Buat prototipe backend, ukur CPU/latency, tetapkan target berbasis hasil |
| Perbedaan perilaku plugin LV2 dan VST3 | Fitur tidak konsisten | Adapter per format dan contract test |
| Berkas audio hilang | Playlist gagal diputar | Tampilkan status missing dan sediakan relink |
| Lisensi library/SDK | Risiko distribusi | Audit lisensi sebelum rilis dan dokumentasikan dependency |

## 15. Tahapan Pengembangan

### Milestone 0 — Technical Spike

- [ ] Build aplikasi Qt Widgets pada Linux.
- [ ] Temukan plugin LV2 menggunakan Lilv.
- [ ] Tampilkan nama, URI, bundle, dan jumlah UI.
- [ ] Uji instansiasi satu plugin LV2.
- [ ] Jalankan pemrosesan DSP pada buffer uji.
- [ ] Buka/tutup satu UI plugin melalui Suil.
- [ ] Dokumentasikan keterbatasan UI dan dependency.

**Exit criteria:** discovery, instansiasi, pemrosesan, dan editor satu plugin teruji secara terpisah.

### Milestone 1 — Player dan Audio Engine

- [ ] Pilih dan integrasikan backend audio.
- [ ] Implementasikan decoder dan transport Main Player.
- [ ] Implementasikan decoder dan transport BGM Player.
- [ ] Implementasikan mixer dan volume.
- [ ] Implementasikan fade-in/fade-out.
- [ ] Uji dua sumber audio bersamaan.

**Exit criteria:** kedua player dapat memutar audio bersamaan tanpa saling mengubah transport atau volume.

### Milestone 2 — Rack dan Sesi

- [ ] Implementasikan model rack.
- [ ] Implementasikan tambah, hapus, enable/disable, dan reorder.
- [ ] Hubungkan rack dengan graph DSP.
- [ ] Simpan/buka sesi.
- [ ] Tangani plugin atau berkas yang missing.

**Exit criteria:** konfigurasi rack dan playlist dapat dipulihkan setelah aplikasi dibuka kembali.

### Milestone 3 — UI Plugin

- [ ] Hubungkan lifecycle plugin dengan editor Suil.
- [ ] Sinkronkan parameter dan status editor.
- [ ] Tambahkan fallback untuk UI yang tidak kompatibel.
- [ ] Uji tambah/hapus plugin ketika editor terbuka.
- [ ] Uji kebocoran resource dan stabilitas.

**Exit criteria:** alur buka/tutup editor stabil untuk daftar plugin uji yang terdokumentasi.

### Milestone 4 — Windows

- [ ] Tentukan versi dan lisensi dependency.
- [ ] Implementasikan adapter VST3.
- [ ] Uji discovery, instansiasi, DSP, parameter, dan editor.
- [ ] Pastikan format sesi menangani plugin lintas platform yang tidak tersedia.
- [ ] Siapkan paket installer dan dokumentasi.

**Exit criteria:** fitur inti player dan rack berjalan pada Windows dengan plugin VST3 yang didukung.

## 16. Rencana Pengujian dan QA

### Functional tests
- Tambah, hapus, dan pindahkan track dalam playlist.
- Uji transport untuk kedua player secara independen.
- Uji fade dengan beberapa durasi.
- Tambah, hapus, reorder, enable/disable plugin.
- Simpan dan buka kembali sesi.
- Buka sesi dengan berkas atau plugin yang hilang.

### Audio tests
- Putar Main Player saja.
- Putar BGM Player saja.
- Putar keduanya bersamaan.
- Uji perubahan volume dan master.
- Uji rack kosong dan rack berisi beberapa plugin.
- Uji perubahan sample rate/buffer yang didukung.
- Dengarkan artefak saat play, pause, stop, seek, fade, dan perubahan rack.

### Stability tests
- Scan katalog berkali-kali.
- Buka/tutup editor berulang kali.
- Tambah/hapus plugin dengan urutan berbeda.
- Tutup aplikasi saat sesi aktif.
- Cabut atau ubah perangkat audio jika sistem mendukung pengujian ini.
- Uji plugin yang tidak tersedia atau gagal diinstansiasi.

### Platform tests
- Uji pada distro Linux yang ditetapkan sebagai target.
- Uji Windows pada versi OS target.
- Uji resolusi layar dan skala UI yang umum.
- Catat versi OS, library, plugin, backend audio, dan perangkat saat melaporkan bug.

## 17. Metrik Keberhasilan

Metrik berikut harus diukur selama pengembangan, bukan diasumsikan tercapai:

- Tingkat keberhasilan discovery pada plugin uji yang didukung.
- Tingkat keberhasilan instansiasi dan pemrosesan plugin uji.
- Tingkat keberhasilan buka/tutup editor tanpa crash.
- Jumlah glitch audio dalam skenario uji yang ditentukan.
- Penggunaan CPU saat kedua player aktif dengan rack kosong dan berisi plugin.
- Keberhasilan save/load sesi.
- Jumlah bug kritis yang tersisa sebelum rilis.

Target numerik ditetapkan setelah baseline prototipe tersedia.

## 18. Pertanyaan Terbuka

1. Backend audio Linux mana yang menjadi target awal?
2. Format audio apa saja yang wajib didukung pada MVP?
3. Apakah rack harus horizontal, vertikal, atau dapat diubah oleh pengguna?
4. Apakah plugin perlu bypass, solo, dan reorder melalui drag-and-drop pada MVP?
5. Apakah perubahan rack boleh menghentikan audio sejenak, atau wajib berlangsung tanpa gangguan?
6. Apakah editor plugin harus selalu tertanam, atau jendela terpisah dapat diterima?
7. Versi Windows minimum apa yang akan didukung?
8. Apakah preset plugin harus kompatibel lintas Linux/Windows jika plugin tersedia pada kedua platform?
9. Apakah diperlukan mode portable atau installer biasa?
10. Target distribusi pertama: source build, paket distro Linux, atau installer mandiri?

## 19. Definisi Selesai (Definition of Done)

Fitur dianggap selesai jika:
- Persyaratan terkait telah diimplementasikan dan ditinjau.
- Kriteria penerimaan lulus pada platform target yang relevan.
- Penanganan kesalahan tersedia untuk kasus gagal yang diketahui.
- Tidak ada operasi blocking yang tidak aman di callback audio.
- Konfigurasi yang relevan disimpan dan dipulihkan dengan benar.
- Dokumentasi penggunaan dan build diperbarui.
- Batasan kompatibilitas yang belum terselesaikan didokumentasikan.

## 20. Referensi Teknis

Referensi ini untuk memulai riset implementasi; keputusan akhir harus divalidasi terhadap versi library dan lisensi yang benar-benar digunakan.

- LV2: https://lv2plug.in/
- Lilv: https://drobilla.net/software/lilv.html
- Suil: https://drobilla.net/software/suil.html
- LSP Plugins: https://lsp-plug.in/
- JUCE: https://juce.com/
- CMake: https://cmake.org/
- Qt: https://www.qt.io/

---

**Catatan akhir:** PRD ini sengaja memisahkan kebutuhan produk dari detail implementasi yang belum dibuktikan. Langkah teknis pertama adalah membuat *technical spike* Linux untuk memastikan discovery, instansiasi DSP, dan lifecycle native UI plugin bekerja pada kombinasi library/plugin yang ditargetkan.
