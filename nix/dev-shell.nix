{ pkgs }:

let
  python = pkgs.python3;
in
pkgs.mkShell {
  packages = with pkgs; [
    boost
    ccache
    clang-tools
    cmake
    ffmpeg
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
    libxcursor
    libxi
    libxinerama
    libxrandr
    libxrender
    libpulseaudio
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
    export PKG_CONFIG_PATH="${pkgs.ffmpeg.dev}/lib/pkgconfig:${pkgs.openssl.dev}/lib/pkgconfig:${pkgs.libpulseaudio}/lib/pkgconfig:$PKG_CONFIG_PATH"
    export LD_LIBRARY_PATH="${pkgs.lib.makeLibraryPath [
      pkgs.ffmpeg
      pkgs.libpulseaudio
      pkgs.openssl
      pkgs.qt6.qtbase
      pkgs.qt6.qt5compat
      pkgs.qt6.qtdeclarative
      pkgs.qt6.qtsvg
      pkgs.libx11
      pkgs.libxrandr
    ]}:$LD_LIBRARY_PATH"
  '';
}
