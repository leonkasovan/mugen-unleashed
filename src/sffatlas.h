#ifndef MUGEN_SFFATLAS_H
#define MUGEN_SFFATLAS_H

/* Port extension. Include after mugen.h. A v3 Sprite borrows its root
 * region from the owning set, including through links and shared sets. */
SpriteSet *Sff3_Load(Stream *stream, long start, const Sff2Header *header, const int *selection);
unsigned Sff_SourceVersion(const SpriteSet *set);
void Sff3_Release(SpriteSet *set);
void Sff3_MemUse(const SpriteSet *set, unsigned *sys, unsigned *tex);
void *Sff3_Pixels(const Sprite *sprite);
GlTex *Sff3_Texture(const Sprite *sprite, float rect[4]);
void Sff3_ForgetTextures(void);

int Sff_AtlasSibling(const char *path, char *out, size_t capacity);
SpriteSet *Sff_LoadSelectedFile(const char *path, const int *selection);
SpriteSet *Sff_LoadZipMember(Stream *zip, const char *path, const int *selection);
#endif
