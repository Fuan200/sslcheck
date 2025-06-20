// SSLCHECK
// Prints the remainder of days of the life of a TLS/SSL Certificate
// Supports json output
// Author: Alexia Michelle <alexia@goldendoglinux.org>
// License: GNU GPL 3.0 (see LICENSE for more information)

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <locale.h>
#include <libintl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "sslcheck.h"

// Arquitectura (para la versión)
const char *get_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386) || defined(_M_IX86)
    return "i386";
#elif defined(__aarch64__)
    return "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__ppc64__) || defined(__powerpc64__)
    return "ppc64";
#elif defined(__ppc__) || defined(__powerpc__)
    return "ppc";
#elif defined(__mips__)
    return "mips";
#else
    return "unknown";
#endif
}

// Plataforma (para la versión)
const char *get_platform() {
    #if defined(__linux__)
        return "Linux";
    #elif defined(__APPLE__) && defined(__MACH__)
        return "macOS";
    #elif defined(__FreeBSD__)
        return "FreeBSD";
    #elif defined(_WIN32)
        return "Windows";
    #else
        return "Unknown";
    #endif
}

// Version
void print_version() {
    printf(_("SSLCHECK %s (%s, %s)\n"), VERSION, get_platform(), get_arch());
    printf("%s\n", AUTHOR);
}

// Ayuda
void print_help() {
    printf(_("SSLCHECK %s (%s, %s)\n"), VERSION, get_platform(), get_arch());
    printf("%s\n\n", AUTHOR);
    printf(_("sslcheck <domain>         prints domain and remainder of days until cert expires\n"));
    printf(_("-s --short <domain>       prints only the days\n"));
    printf(_("-j --json <domain>        prints output as JSON\n"));
    printf(_("-p --port <port>          use custom port instead of 443\n"));
    printf(_("-h --help                 prints this menu\n"));
    printf(_("-v --version              prints version\n"));
}

// Calcula días hasta expiración
int days_until_expiration(X509 *cert) {
    const ASN1_TIME *notAfter = X509_get0_notAfter(cert);
    ASN1_TIME *asn1_now = ASN1_TIME_new();
    ASN1_TIME_set(asn1_now, time(NULL));

    int days = 0, seconds = 0;
    ASN1_TIME_diff(&days, &seconds, asn1_now, notAfter);
    ASN1_TIME_free(asn1_now);
    return days;
}

int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    bindtextdomain("sslcheck", LOCALE_PATH);
    textdomain("sslcheck");

    int short_output = 0;
    int json_output = 0;
// variable port set to 5 characters long (65535) plus the null terminator
// this is enough for the largest possible port.
// This is a security improvement.
// - Alexia
    char port[6] = "443"; 

    static struct option long_options[] = {
        {"short",   no_argument,       0, 's'},
        {"json",    no_argument,       0, 'j'},
        {"port",    required_argument, 0, 'p'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "sjp:hv", long_options, NULL)) != -1) {
        switch (opt) {
            case 's': short_output = 1; break;
            case 'j': json_output = 1; break;
            case 'p':
  // security improvement to always terminate and not overflow if given a very large string

	    snprintf(port, sizeof(port), "%s", optarg);

                break;
            case 'h': print_help(); return 0;
            case 'v': print_version(); return 0;
            default:
                print_help();
                return EXIT_FAILURE;
        }
    }

    if (optind >= argc) {
        print_help();
        return EXIT_FAILURE;
    }

    const char *hostname = argv[optind];

    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    const SSL_METHOD *method = TLS_client_method();
    SSL_CTX *ctx = SSL_CTX_new(method);
    if (!ctx) {
        if (json_output)
            printf("{\"domain\": \"%s\", \"days\": null}\n", hostname);
        else
            fprintf(stderr, _("Error creating SSL context\n"));
        return EXIT_FAILURE;
    }

    SSL *ssl = SSL_new(ctx);
    BIO *bio;
    char target[512];
    snprintf(target, sizeof(target), "%s:%s", hostname, port);

    bio = BIO_new_ssl_connect(ctx);
    BIO_get_ssl(bio, &ssl);
    if (!ssl) {
        if (json_output)
            printf("{\"domain\": \"%s\", \"days\": null}\n", hostname);
        else
            fprintf(stderr, _("Error getting SSL object\n"));
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return EXIT_FAILURE;
    }

    SSL_set_tlsext_host_name(ssl, hostname);
    BIO_set_conn_hostname(bio, target);

    if (BIO_do_connect(bio) <= 0) {
        if (json_output)
            printf("{\"domain\": \"%s\", \"days\": null}\n", hostname);
        else
            fprintf(stderr, _("Error connecting to %s\n"), hostname);
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return EXIT_FAILURE;
    }

    X509 *cert = SSL_get_peer_certificate(ssl);
    if (!cert) {
        if (json_output)
            printf("{\"domain\": \"%s\", \"days\": null}\n", hostname);
        else
            fprintf(stderr, _("No certificate found for %s\n"), hostname);
        BIO_free_all(bio);
        SSL_CTX_free(ctx);
        return EXIT_FAILURE;
    }

    int days = days_until_expiration(cert);
    if (days < 0) {
        if (json_output)
            printf("{\"domain\": \"%s\", \"days\": null}\n", hostname);
        else
            fprintf(stderr, _("Could not calculate certificate expiration\n"));
    } else {
        if (json_output) {
            printf("{\"domain\": \"%s\", \"days\": %d}\n", hostname, days);
        } else if (short_output) {
            printf("%d\n", days);
        } else {
            printf(_("Domain: %s | Days until Certification expires: %d\n"), hostname, days);
        }
    }

    X509_free(cert);
    BIO_free_all(bio);
    SSL_CTX_free(ctx);
    EVP_cleanup();
    ERR_free_strings();

    return EXIT_SUCCESS;
}
