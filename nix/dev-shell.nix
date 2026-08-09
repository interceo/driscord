{ pkgs }:

let
  python = pkgs.python3;
in
pkgs.mkShell {
  packages = with pkgs; [
    alsa-lib
    boost
    ccache
    clang
    clang-tools
    cmake
    gnutls
    gdb
    git
    jq
    ninja
    openssl
    pkg-config
    postgresql
    python
    qt6.qtbase
    qt6.qt5compat
    qt6.qtdeclarative
    qt6.qtsvg
    qt6.qtwayland
    libice
    libsm
    libx11
    libxcomposite
    libxdamage
    libxext
    libxfixes
    libxcursor
    libxi
    libxinerama
    libxrandr
    libxrender
    libxtst
    libpulseaudio
    lld
    nss
  ];

  env = {
    CMAKE_GENERATOR = "Ninja";
    DRISCORD_NIXOS_ENV = "1";
  };

  shellHook = ''
    export QT_QPA_PLATFORM=''${QT_QPA_PLATFORM:-xcb}
    export QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtsvg}/${pkgs.qt6.qtbase.qtPluginPrefix}:${pkgs.qt6.qtwayland}/${pkgs.qt6.qtbase.qtPluginPrefix}:$QT_PLUGIN_PATH"
    export QML_IMPORT_PATH="${pkgs.qt6.qtdeclarative}/${pkgs.qt6.qtbase.qtQmlPrefix}:${pkgs.qt6.qt5compat}/${pkgs.qt6.qtbase.qtQmlPrefix}:$QML_IMPORT_PATH"
    export QML2_IMPORT_PATH="$QML_IMPORT_PATH"
    export PKG_CONFIG_PATH="${pkgs.openssl.dev}/lib/pkgconfig:${pkgs.libpulseaudio}/lib/pkgconfig:$PKG_CONFIG_PATH"
    export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [
      # WebRTC's Linux audio backends are dlopen'd, not linked, so libasound
      # and libpulse have to be resolvable at runtime rather than at link time.
      pkgs.alsa-lib
      pkgs.gnutls
      pkgs.libpulseaudio
      pkgs.nss
      pkgs.openssl
      pkgs.qt6.qtbase
      pkgs.qt6.qt5compat
      pkgs.qt6.qtdeclarative
      pkgs.qt6.qtsvg
      # WebRTC's DesktopCapturer pulls in the X11 extensions, and the archive
      # is built against the process libstdc++ rather than a custom libc++.
      pkgs.stdenv.cc.cc.lib
      pkgs.libglvnd
      pkgs.libx11
      pkgs.libxcomposite
      pkgs.libxdamage
      pkgs.libxext
      pkgs.libxfixes
      pkgs.libxrandr
      pkgs.libxtst
    ]}:$LD_LIBRARY_PATH"
  '';
}
