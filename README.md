![Logo](https://gitlab.com/gateship-one/malp/raw/master/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png)
# M.A.L.P. - Android MPD Client #

[<img src="https://f-droid.org/badge/get-it-on.png" alt="Get it on F-Droid" height="80">](https://f-droid.org/app/org.gateshipone.malp)

This whole project is licensed under the  **GPLv3 or later** license (see LICENSE)

**[Screenshots](https://gitlab.com/gateship-one/malp/wikis/Screenshots)**

**Please check out the [Frequently Asked Questions](https://gitlab.com/gateship-one/malp/wikis/FAQ)**

## Requirements: ##
 - Android 5.0
 - MPD >= 0.14 (Everything < 0.23 is considered experimental going forward)

## Features: ##
 - Artist/Album/Files library browsing
 - AlbumArtist filtering
 - MBID filtering (multiple albums with same AlbumArtist, Title can be accessed separately)
 - Server-based search
 - Local fast search
 - Launcher widget
 - Notification with background service (**optional, can be disabled in the settings**)
 - Volume control if application is in background (**only if notification is enabled & visible**)
 - Mostly optimized for tablets
 - Basic playlist management (add songs to saved lists, remove songs from saved lists, remove lists, save lists)
 - Multiple server profiles
 - Server statistics **(here is also the update database button)**
 - Artwork support with MusicBrainz, Last.fm, Fanart.tv as artwork provider (Album, artist images and fullscreen fanart)
 - Fanart view for tablets or stuff like that (click on the cover in the now playing view, or select fullscreen mode from menu)
 - Bulk downloader to download all artwork to local memory
 - HTTP audio streaming from MPD server
 - HTTP cover download
 - MPD artwork loading
 - Partitions (create, delete, move outputs)
 - Tag browser (filter Artists, Albums, Tracks) by all available MPD tags (e.g. genre, performer, composer, label, ...)

## YouTube / yt-dlp / Yoinks-MPD integration (experimental design)

This fork contains an experimental design area for receiving YouTube shares in MALP and forwarding audio to an existing MPD server on a Raspberry Pi. The intended media path uses **yt-dlp stdout and a Linux FIFO (named pipe)**; it must not create a complete audio file or a temporary media download.

- Design, limitations and implementation plan: [docs/yoinks-mpd/README.md](docs/yoinks-mpd/README.md)
- Bash producer prototype: [scripts/yoinks-mpd/yt-dlp-to-fifo.sh](scripts/yoinks-mpd/yt-dlp-to-fifo.sh)
- Existing YouTube integration audit: [docs/YOUTUBE_AUDIO_BRIDGE_AUDIT.md](docs/YOUTUBE_AUDIO_BRIDGE_AUDIT.md)

**Important:** this is an experimental integration, not a verified end-to-end MPD input implementation. MPD's documented `pipe` output plugin writes decoded PCM *from* MPD to a command; it is not an input pipe for feeding YouTube audio into MPD. The FIFO/file-input route must be verified against the exact MPD build and configuration before it is treated as operational. No successful playback or Raspberry Pi test is claimed here.

The integration scripts and documentation use Bash and native command-line tools (including yt-dlp, MPD/MPС utilities and optionally FFmpeg). **Python is not used.** Yoinks is treated as the user-facing workflow/integration concept; the original interactive Yoinks TUI is not assumed to provide a stream-to-MPD mode.

## Description ##

This is M.A.L.P. a fast and easy to use MPD client. The main goal is to create a MPD client that is both simple and fast to use and also follow the material design guidelines.

This MPD client is tested with MPD 0.14, 0.19 and 0.23 but you really should not use outdated versions of MPD.

Support for outdated versions is experimental.

**This MPD client works best with accurately tagged music libraries. I recommend tagging using [MusicBrainz Picard](https://picard.picard.org/)**

Starting with MPD 0.19 it is possible to filter list requests which allows this client to use the AlbumArtist and Artist tag for retrieval of albums. So if you have a lot of "Greatest Hits" albums you will get a list entry for each one of it.
