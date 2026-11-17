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

## YouTube / yt-dlp / Yoinks-MPD integration (experimental)

This fork contains an experimental design area for receiving YouTube shares in MALP and forwarding audio to an existing MPD server on a Raspberry Pi. The **target architecture is a local C++ HTTP streaming relay consumed by MPD's CURL input plugin**. Audio is relayed progressively with bounded RAM buffering and back-pressure; complete media files must never be written to persistent storage.

- Architecture, security and implementation plan: [professional audit](work/youtube-audio-bridge/AUDYT_ARCHITEKTURY_I_PLAN_REALIZACJI.md)
- Current stage tracker: [YouTube audio bridge workspace](work/youtube-audio-bridge/README.md)
- Pi HTTP-input diagnostic: [diagnose-mpd-http.sh](scripts/yoinks-mpd/diagnose-mpd-http.sh)
- Diagnostic procedure: [MPD HTTP input verification](docs/yoinks-mpd/MPD_HTTP_INPUT_DIAGNOSTIC.md)

**Status:** HTTP proxy is the only target transport. No end-to-end playback is claimed because the C++ proxy service is not yet implemented. The exact MPD build on the target Raspberry Pi must pass the CURL-input compatibility gate before integration. The diagnostic script collects version/build evidence but does not itself prove playback.

The Raspberry Pi implementation is restricted to Bash/C++; Android remains Java. RAM-only is a hard requirement, including disabling disk-backed swap or proving that bridge pages cannot spill to persistent storage. No merge to the default branch before hardware playback, queue, RAM-only and regression gates pass.

## Description ##

This is M.A.L.P. a fast and easy to use MPD client. The main goal is to create a MPD client that is both simple and fast to use and also follow the material design guidelines.

This MPD client is tested with MPD 0.14, 0.19 and 0.23 but you really should not use outdated versions of MPD.

Support for outdated versions is experimental.

**This MPD client works best with accurately tagged music libraries. I recommend tagging using [MusicBrainz Picard](https://picard.picard.org/)**

Starting with MPD 0.19 it is possible to filter list requests which allows this client to use the AlbumArtist and Artist tag for retrieval of albums. So if you have a lot of "Greatest Hits" albums you will get a list entry for each one of it.
