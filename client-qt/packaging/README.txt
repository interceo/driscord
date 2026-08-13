Driscord desktop client
=======================

Run the application from this directory:

  ./driscord

Useful commands:

  ./driscord --version
  ./driscord --help

The signaling and API endpoints are fixed when this client is built. They are
shown in the build's CMake configuration and are not stored in a runtime file.

Optional user settings are read from config.json in the platform configuration
directory (~/.config/driscord on Linux). A config.json in the current working
directory is accepted as a fallback for development and portable installations.
The current JSON settings are screen_fps, turn_servers and stun_servers.

The launcher ignores host Qt/QML and dynamic-library search paths, so the client
uses the libraries and plugins shipped in this archive.
