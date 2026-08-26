/* Appended to MbedTLS' default config through MBEDTLS_USER_CONFIG_FILE
 * (root CMakeLists.txt, Windows client TLS backend).
 *
 * libdatachannel compiles its DTLS transport against the DTLS-SRTP API
 * unconditionally, so the option has to be on even though the client's
 * NO_MEDIA build never negotiates SRTP profiles.
 */
#define MBEDTLS_SSL_DTLS_SRTP
