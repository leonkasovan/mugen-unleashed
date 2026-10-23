#ifndef MUGEN_MODEL_H
#define MUGEN_MODEL_H

/*
 * glTF 2.0 models (model.c) and the 3D stages built on them (stage3d.c).
 *
 * mugen-unleashed addition, not part of the original: a port of the 3D stage
 * support of Ikemen GO (model.go, stage.go), so that the stages made for it
 * ([BGdef] model, [Model] offset / scale, [Camera] fov) look the same here.
 */

/* Most joints of a skin the vertex shader takes. */
#define MODEL_MAX_JOINTS 64

/* Layer of the nodes without a layerNumber extra (Model_Draw). */
#define MODEL_LAYER_DEFAULT (-0x7FFFFFFF)

/* ModelNode.trans: the node's "trans" extra (blending of its meshes). */
enum {
    MODEL_TRANS_NONE = 0,
    MODEL_TRANS_ADD = 1,
    MODEL_TRANS_SUB = 2,
    MODEL_TRANS_MUL = 3
};

/* ModelMaterial.alpha_mode: the glTF alphaMode. */
enum {
    MODEL_ALPHA_OPAQUE = 0,
    MODEL_ALPHA_MASK = 1,
    MODEL_ALPHA_BLEND = 2
};

/* ModelSampler.interp: the glTF sampler interpolation. */
enum {
    MODEL_INTERP_LINEAR = 0,
    MODEL_INTERP_STEP = 1,
    MODEL_INTERP_CUBIC = 2
};

/* ModelChannel.path: the animated property of a node. */
enum {
    MODEL_PATH_TRANSLATION = 0,
    MODEL_PATH_ROTATION = 1,
    MODEL_PATH_SCALE = 2
};

/* ModelPrim.attribs: the vertex attributes a primitive has. */
#define MODEL_ATTR_UV       0x01
#define MODEL_ATTR_COLOR    0x02
#define MODEL_ATTR_SKIN     0x04

/* ModelNode.animated: the properties an animation currently sets. */
#define MODEL_ANIMATED_T    0x01
#define MODEL_ANIMATED_R    0x02
#define MODEL_ANIMATED_S    0x04

/* A material: the base colour (texture and factor) and how it is blended. */
typedef struct ModelMaterial {
    int alpha_mode;             /* MODEL_ALPHA_* */
    float alpha_cutoff;         /* alphaCutoff with MASK (0.5 by default), else 0 */
    int texture;                /* base colour texture (Model.textures), -1 for none */
    float tex_transform[9];     /* KHR_texture_transform of the texture coordinates (mat3, column-major) */
    float base_color[4];        /* baseColorFactor */
    int double_sided;
    int unlit;                  /* KHR_materials_unlit */
} ModelMaterial;

/*
 * A primitive: its vertices are blocks of the model's vertex buffer, one per
 * attribute (position, then uv, colour, joints and weights when present), its
 * indices 32-bit indices in the index buffer.
 */
typedef struct ModelPrim {
    unsigned int mode;          /* GL primitive mode */
    int vertex_count;
    int index_count;
    unsigned int vertex_offset; /* byte offset of the vertex data */
    unsigned int index_offset;  /* byte offset of the indices */
    int material;               /* -1: not drawn (as Ikemen GO does) */
    unsigned int attribs;       /* MODEL_ATTR_* */
} ModelPrim;

typedef struct ModelMesh {
    int first_prim;             /* Model.prims */
    int prim_count;
} ModelMesh;

/*
 * A node. The translation, rotation and scale are the rest values, replaced
 * by the anim_* values while an animation sets them; the local transform is
 * computed again when they change.
 */
typedef struct ModelNode {
    float translation[3];
    float rotation[4];          /* quaternion x, y, z, w */
    float scale[3];
    float anim_translation[3];
    float anim_rotation[4];
    float anim_scale[3];
    unsigned int animated;      /* MODEL_ANIMATED_* */
    int changed;                /* the local transform needs computing */
    int has_matrix;             /* a "matrix" node (unless animated, local is that matrix) */
    float local[16];            /* column-major, as GL */
    float world[16];
    int mesh;                   /* -1 for none */
    int skin;                   /* -1 for none */
    int *children;
    int child_count;
    int trans;                  /* MODEL_TRANS_* ("trans" extra) */
    int ztest;                  /* 0 with a "disableZTest" extra */
    int zwrite;                 /* 0 with a "disableZWrite" extra */
    int layer;                  /* "layerNumber" extra, MODEL_LAYER_DEFAULT when absent */
    int id;                     /* "id" extra (BGCtrl3D) */
    int visible;
} ModelNode;

typedef struct ModelSkin {
    int *joints;                /* nodes */
    int joint_count;
    float *inverse_bind;        /* 16 floats per joint */
    float *matrices;            /* per joint, the rows of its 3x4 joint matrix (12 floats), for the shader */
} ModelSkin;

typedef struct ModelSampler {
    float *times;               /* input */
    int key_count;
    float *values;              /* output: key_count (x3 with CUBICSPLINE) values of `comps` floats */
    int comps;                  /* 3 or 4 */
    int interp;                 /* MODEL_INTERP_* */
} ModelSampler;

typedef struct ModelChannel {
    int node;
    int path;                   /* MODEL_PATH_* */
    int sampler;                /* ModelAnim.samplers */
} ModelChannel;

/*
 * An animation. It runs while enabled, 1/60 s per tick, looping loop_count
 * times (-1 forever); "id", "loopCount" and "enabled" come from its extras.
 */
typedef struct ModelAnim {
    int id;
    float duration;
    float time;
    int loop_count;
    int loop;
    int enabled;
    int default_enabled;
    ModelChannel *channels;
    int channel_count;
    ModelSampler *samplers;
    int sampler_count;
} ModelAnim;

typedef struct ModelScene {
    int *nodes;                 /* the root nodes */
    int node_count;
} ModelScene;

typedef struct Model {
    ModelScene *scenes;
    int scene_count;
    ModelNode *nodes;
    int node_count;
    ModelMesh *meshes;
    int mesh_count;
    ModelPrim *prims;
    int prim_count;
    ModelMaterial *materials;
    int material_count;
    unsigned int *textures;     /* GL textures */
    int texture_count;
    ModelAnim *anims;
    int anim_count;
    ModelSkin *skins;
    int skin_count;
    unsigned int vbo;           /* GL buffers, 0 without the OpenGL renderer */
    unsigned int ibo;
    unsigned char *vertex_data; /* the buffer contents until they are uploaded */
    unsigned int vertex_size;
    unsigned int *index_data;
    unsigned int index_count;
} Model;

/* PalFX terms of a model draw, as the sprite programs take them (Glr_FxTerms). */
typedef struct ModelFx {
    float add[3];
    float mul[3];
    float colorbal;
    int invertall;
} ModelFx;

/* The 3D model of the loaded stage and how it is placed (stage3d.c). */
typedef struct Stage3D {
    Model *model;               /* [BGdef] model, NULL for a 2D stage */
    float offset[3];            /* [Model] offset */
    float scale[3];             /* [Model] scale */
    float fov;                  /* [Camera] fov, degrees (40) */
    float near_z;               /* [Camera] near (0.1) */
    float far_z;                /* [Camera] far (10000) */
    float yshift;               /* [Camera] yshift */
    int localcoord_h;           /* [StageInfo] localcoord height */
} Stage3D;

extern Stage3D g_stage3d;

Model *Model_Load(const char *path);
void Model_Free(Model *mdl);
void Model_Step(Model *mdl);
void Model_Reset(Model *mdl);
void Model_Draw(Model *mdl, int scene, int layer, int default_layer, const float *proj, const float *view,
                const ModelFx *fx);

void Stage3D_Load(Stage *st, Ini *ini, const char *path);
void Stage3D_Free(void);
void Stage3D_Tick(void);
void Stage3D_Reset(void);
void Stage3D_Draw(int layer);

#endif /* MUGEN_MODEL_H */
