// Implémentations des bibliothèques de lecture d'images (fichiers d'en-tête
// seuls, dans external/) : stb_image (PNG, JPEG, HDR) et tinyexr (OpenEXR).
// tinyexr utilise le décompresseur zlib de stb_image au lieu de miniz.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_HDR
#include <stb_image.h>

#include <cstdlib>

// tinyexr ne sait qu'écrire avec cette fonction de stb_image_write : on ne
// fait que lire, elle n'est jamais appelée.
extern "C" unsigned char* stbi_zlib_compress(unsigned char*, int, int* outLen, int)
{
    *outLen = 0;
    return nullptr;
}

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>
