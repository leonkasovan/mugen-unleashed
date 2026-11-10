/* Port optimization: locations belong to one successfully linked program.
 * Keep this separate from GlProgram so the original 32-bit ABI is unchanged. */
#ifndef MUGEN_VITA_VERTEX_INTERFACE_H
#define MUGEN_VITA_VERTEX_INTERFACE_H
typedef struct VitaGlVertexInterface {
    int position;
    int texcoord;
    int matrix;
} VitaGlVertexInterface;
#endif
