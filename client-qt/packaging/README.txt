Driscord desktop client
=======================

On Linux the client ships as a single AppImage — make it executable and run
it (FUSE is required to mount it; without FUSE append --appimage-extract-and-run):

  chmod +x driscord-client-*.AppImage
  ./driscord-client-*.AppImage
  ./driscord-client-*.AppImage --version

On Windows the client is a portable directory; run driscord_client.exe.

The signaling and API endpoints are fixed when this client is built. They are
shown in the build's CMake configuration and are not stored in a runtime file.

Optional user settings are read from config.json in the platform configuration
directory (~/.config/driscord on Linux, %LOCALAPPDATA%/driscord on Windows).
The current JSON settings are screen_fps, turn_servers and stun_servers;
development (non-release) builds additionally accept update_url, update_channel
and update_public_key to point the updater at a test channel.

The client updates itself from the release channel: it periodically checks the
signed manifest, and Settings -> Advanced offers a manual check, the download
and the restart-and-install step. On Linux installing replaces the AppImage
file in place (shortcuts keep working); on Windows it swaps the files of the
portable directory. Either way the previous version is kept under .update/old
next to the install until the next successful start.

The launcher ignores host Qt/QML and dynamic-library search paths, so the
client uses the libraries and plugins shipped in the bundle.
