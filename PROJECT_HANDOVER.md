# UltraVNC (OpView) - Proje Devir Notlari

Bu dosya, su ana kadar yapilan tum degisiklikleri, build akislarini, calistirma sekillerini ve sonraki oturumlarda hizli devam icin gerekli teknik ozeti icerir.

## 1) Hedef ve Kapsam
- Calisma alani: `winvnc.exe` (server)
- Ana hedefler:
  - Konfigu dis dosyadan okumayi kapatip kod icine gommek
  - OpView rebrand
  - GitHub Actions ile otomatik derleme
  - CLI parametreleri ile runtime kontrol (port, ekran modu, bildirim, overlay)

## 2) Repo ve CI Bilgisi
- Repo: `https://github.com/senhakan/UltraVNC`
- Branch: `main`
- Workflow: `Build winvnc.exe`
- Workflow dosyasi: `.github/workflows/build-winvnc.yml`
- Artifact adi: `winvnc-x64-Release`

## 3) Su Anki Mimari Karar
### 3.1 Konfig Okuma
- `SettingsManager::Initialize()` dis config/ini yuklemez.
- `SettingsManager::load()` disk/registry okumaz; `setDefaults()` cagirir.
- Uygulama calisma ayarlari kod icinde defaultlardan gelir.

### 3.2 Runtime Override Mantigi (Kritik)
- CLI ile verilen runtime override degerleri `SettingsManager` icinde ayrica tutulur.
- `setDefaults()/load()` cagrilarindan sonra tekrar uygulanir.
- Bu sayede startup sirasinda ayarlar reset olsa bile CLI override kaybolmaz.

## 4) Statik Defaultlar (Gomulu)
Kaynak: `winvnc/winvnc/SettingsManager.cpp`
- `PortNumber = 20010`
- `AutoPortSelect = 0`
- `HTTPConnect = 0`
- `Notification = 0` (default kapali)
- `DisableTrayIcon = 0` (default goster)
- `Frame = 1`
- Overlay icin OSD sadece parametreyle zorlanir.

## 5) Dinamik Parametreler (CLI)
Kaynak: `winvnc/winvnc/winvnc.h` ve `winvnc/winvnc/winvnc.cpp`

### 5.1 Ekran modu
- `-displaymode primary`
- `-displaymode secondary`
- `-displaymode all`
Davranis:
- `secondary` + 2.monitor yok => sessiz `primary` fallback
- `all` + tek monitor => tek monitor paylasim
- Hata/log zorlugu yok (istenen sekilde)

### 5.2 Port
- `-port <1..65535>`
- `AutoPortSelect` kapatilir, RFB port override edilir.

### 5.3 Bildirim ve tray
- `-notification` => bildirimleri acar
- `-hidetrayicon` => tray icon gizlenir
- Parametre yoksa default: bildirim kapali, tray gorunur

### 5.4 Baglanti overlay
- `-connectionoverlay`
- Baglanti aktifken frame+OSD zorlanir.

### 5.5 Overlay user metni
- `-user "<kullanici>"`
- Overlay metni su formatta uretilir:
  - `Uzaktan destek aktif! Bagli kullanici: <kullanici>`
- `-user` verilmezse fallback metin:
  - `OpView session is active on this device`

## 6) Overlay Gorsel Davranis
Kaynak: `winvnc/winvnc/LayeredWindows.cpp`
- Overlay metni merkezde cizilir (`DT_CENTER | DT_VCENTER | DT_WORDBREAK`).
- Kirmizi cerceve ile birlikte gorunur.
- Sadece aktif baglanti sirasinda gorunur.

## 7) Icon Sistemi
- Ana icon kaynagi `opview on-18.svg`'den uretilen multi-size `.ico`
- Guncellenen dosyalar:
  - `winvnc/winvnc/res/world3a.ico`
  - `winvnc/winvnc/res/icon2.ico`
- Tray icon yukleme dis dosya fallback yerine resource tabanli sabitlendi:
  - `winvnc/winvnc/vncmenu.cpp`

## 8) Kullanima Hazir Komut Ornekleri
### 8.1 Temel calistirma
- `winvnc.exe -run`

### 8.2 Port + overlay + user
- `winvnc.exe -run -port 5980 -connectionoverlay -user "hakan.sen"`

### 8.3 Tum monitorler + bildirim + tray gizli
- `winvnc.exe -run -displaymode all -notification -hidetrayicon -port 24444`

## 9) Onemli Isletim Notlari
- CLI override parse case-insensitive; switch sirasi fark etmez.
- Ancak `-user` degeri boslukluysa cift tirnakla verilmeli.
- Overlay sadece viewer baglandiginda gorunur; server acilisinda tek basina gorunmez.

## 10) Son Commit Zinciri (ozet)
- `8a7d6ffe` center overlay text
- `0980d259` `-user` runtime overlay text
- `0294e7f5` `-connectionoverlay`
- `a418586f` runtime override persistence
- `c5c5c7cd` override parse order fix
- `9be052d4` app/tray icon switch (OpView)
- `43867c73` notification/tray CLI toggles
- `87493c3c` default port 20010 + `-port`
- `73c57ef1` displaymode override
- `cf8a2d69` embedded defaults + external settings read disabled

## 11) Sonraki Oturumda Hizli Kontrol Listesi
1. Son artifact indir (`winvnc-x64-Release`)
2. Test et:
   - `-run -port 5980`
   - `-run -connectionoverlay -user "test.user"`
   - `-run -displaymode secondary` (2.monitor var/yok senaryosu)
3. Netstat ile dogrula:
   - `netstat -ano | findstr :5980`
4. Overlay metni merkezde ve beklenen formatta mi kontrol et.

## 12) Acik / Opsiyonel Iyilestirmeler
- `-user` icin karakter filtreleme/genisletilmis sanitizasyon
- `-overlaytext` gibi serbest metin parametresi
- Kullanici tarafindan "baglantiyi kes" butonu (ayri interaktif pencere)
- Kullanici dokumani icin sade PDF/README turevi hazirlanmasi

## 13) Guvenlik Notu
- Sohbette kullanilan PAT degerleri ifsa sayilir.
- GitHub tarafinda token rotate/revoke edilmesi onerilir.

