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
    MODEL_PATH_SCALE = 2,
    MODEL_PATH_WEIGHTS = 3,     /* morph target weights of a node */
    MODEL_PATH_MESH_WEIGHTS = 4,/* morph target weights of a mesh (KHR_animation_pointer) */
    MODEL_PATH_FLOATS = 5       /* material or light values (KHR_animation_pointer) */
};

/* ModelPrim.attribs: the vertex attributes a primitive has. */
#define MODEL_ATTR_UV       0x01
#define MODEL_ATTR_COLOR    0x02
#define MODEL_ATTR_SKIN     0x04
#define MODEL_ATTR_NORMAL   0x08
#define MODEL_ATTR_TANGENT  0x10
#define MODEL_ATTR_OUTLINE  0x20    /* _OUTLINE_ATTRIBUTE: direction (xyz) and width (w) of the outline */

/* Morph targets of a primitive at most at once (their position, normal, tangent, uv and colour count apart). */
#define MODEL_MAX_MORPH_SLOTS 8

/* Morph target weights of a mesh at most. */
#define MODEL_MAX_WEIGHTS 64

/* Lights a scene is lit by (the first ones of KHR_lights_punctual in it). */
#define MODEL_MAX_LIGHTS 4

/* ModelLight.type, as the shader numbers them. */
enum {
    MODEL_LIGHT_DIRECTIONAL = 0,
    MODEL_LIGHT_POINT = 1,
    MODEL_LIGHT_SPOT = 2
};

/* Shadow map parameters of a light or light node (extras); 0 keeps the default. */
enum {
    MODEL_SHADOW_NEAR = 0,
    MODEL_SHADOW_FAR,
    MODEL_SHADOW_BOTTOM,
    MODEL_SHADOW_TOP,
    MODEL_SHADOW_LEFT,
    MODEL_SHADOW_RIGHT,
    MODEL_SHADOW_BIAS,
    MODEL_SHADOW_PARAMS
};

/* A KHR_lights_punctual light. */
typedef struct ModelLight {
    int type;                   /* MODEL_LIGHT_* */
    float color[3];
    float intensity;
    float range;                /* not above 0: unlimited */
    float inner_cone;           /* spot cone angles, radians */
    float outer_cone;
    float shadow[MODEL_SHADOW_PARAMS];  /* "shadowMapNear"... extras */
} ModelLight;

/* A texture of a material and the KHR_texture_transform of its coordinates. */
typedef struct ModelTexRef {
    int texture;                /* Model.textures, -1 for none */
    float offset[2];            /* KHR_texture_transform (animatable) */
    float rotation;
    float scale[2];
    float transform[9];         /* the three as a mat3, column-major */
} ModelTexRef;

/* ModelNode.animated: the properties an animation currently sets. */
#define MODEL_ANIMATED_T    0x01
#define MODEL_ANIMATED_R    0x02
#define MODEL_ANIMATED_S    0x04
#define MODEL_ANIMATED_W    0x08

/* A material: the base colour (texture and factor), the PBR parameters and how it is blended. */
typedef struct ModelMaterial {
    int alpha_mode;             /* MODEL_ALPHA_* */
    float alpha_cutoff;         /* alphaCutoff with MASK (0.5 by default), else 0 */
    ModelTexRef base;           /* base colour texture */
    ModelTexRef normal;         /* normal map */
    ModelTexRef metal_rough;    /* metallic (b) / roughness (g) map */
    ModelTexRef occlusion;      /* ambient occlusion map (r) */
    ModelTexRef emissive;       /* emission map */
    float base_color[4];        /* baseColorFactor */
    float metallic;             /* metallicFactor */
    float roughness;            /* roughnessFactor */
    float occlusion_strength;   /* occlusionTexture strength, 0 without the texture */
    float emission[3];          /* emissiveFactor */
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
    /*
     * Morph targets: the vertex blocks of the primitive as loaded (base) and,
     * per target and attribute, the deltas (NULL when the target lacks it);
     * the blocks are morphed on the CPU when the weights change.
     */
    int target_count;
    float *base;                /* the blocks from position to colour, as in the vertex buffer */
    float **deltas;             /* [target * 5 + MODEL_MORPH_*] */
    float applied[MODEL_MAX_WEIGHTS];   /* the weights the buffer holds */
    int applied_count;          /* -1: the base */
} ModelPrim;

/* Morphed attributes, in the order Ikemen GO applies them. */
enum {
    MODEL_MORPH_POSITION = 0,
    MODEL_MORPH_NORMAL,
    MODEL_MORPH_TANGENT,
    MODEL_MORPH_UV,
    MODEL_MORPH_COLOR,
    MODEL_MORPH_ATTRIBS
};

typedef struct ModelMesh {
    int first_prim;             /* Model.prims */
    int prim_count;
    int weight_count;           /* its morph target weights */
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
    float matrix[16];           /* its matrix */
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
    int cast_shadow;            /* 0 with a "castShadow" extra of "0" or "false" */
    int light;                  /* KHR_lights_punctual light (Model.lights), -1 for none */
    float light_dir[3];         /* direction of the light: -z of the node's world rotation */
    float shadow[MODEL_SHADOW_PARAMS];  /* "shadowMapNear"... extras, over the light's */
    float normal_matrix[16];    /* inverse transpose of world (mesh nodes) */
    float outline;              /* "meshOutline" extra: width of the mesh outline, 0 for none */
    float *weights;             /* morph target weights (the node's, else the mesh's) */
    float *anim_weights;        /* the weights an animation sets */
    int weight_count;
    int mesh_weights;           /* the weights are the mesh's (its weight animations apply) */
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
    int elem;                   /* weights: the one weight animated, -1 for all */
    int mesh;                   /* MODEL_PATH_MESH_WEIGHTS */
    float *target;              /* MODEL_PATH_FLOATS: the values animated */
    int comps;                  /* how many */
    float rest[4];              /* their values without the animation */
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
    int *light_nodes;           /* the nodes with a light, children before their parent */
    int light_count;
} ModelScene;

/*
 * The environment map of a model (image based lighting): from an
 * equirectangular Radiance HDR panorama, the Lambertian (diffuse) cube map,
 * the GGX (specular) cube map with a mip level per roughness and the BRDF
 * table, all prefiltered at load time as Ikemen GO does.
 */
typedef struct ModelEnv {
    unsigned int lambertian;    /* GL cube map textures */
    unsigned int ggx;
    unsigned int lut;           /* GL 2D texture */
    int mip_count;              /* mip levels of ggx that are filtered */
    float intensity;            /* [Model] environmentintensity (1) */
} ModelEnv;

typedef struct Model {
    ModelEnv *env;              /* [Model] environment, NULL for none */
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
    ModelLight *lights;
    int light_count;
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

/* Stage3DCtrl.type: what a [BGCtrl3D] does. */
enum {
    S3D_CTRL_NULL = 0,          /* any other type: nothing on 3D nodes */
    S3D_CTRL_ANIM = 1,          /* value 0 switches the animations off, else on from their start */
    S3D_CTRL_VISIBLE = 2        /* value 0 hides the nodes, else shows them */
};

/* A [BGCtrl3D] of the stage (Ikemen GO's bgCtrl with node and animation targets). */
typedef struct Stage3DCtrl {
    int type;                   /* S3D_CTRL_* */
    int start;                  /* time = start, end, loop (stage ticks); loop from [BGCtrlDef] looptime */
    int end;
    int loop;
    int value;
    int *nodes;                 /* the nodes whose "id" extra is in ctrlid */
    int node_count;
    int *anims;                 /* the animations whose "id" extra is in ctrlid */
    int anim_count;
} Stage3DCtrl;

/* The 3D model of the loaded stage and how it is placed (stage3d.c). */
typedef struct Stage3D {
    Model *model;               /* [BGdef] model, NULL for a 2D stage */
    Stage3DCtrl *ctrls;         /* [BGCtrl3D] */
    int ctrl_count;
    int time;                   /* ticks since the stage was reset, as Ikemen GO's stageTime */
    float offset[3];            /* [Model] offset */
    float scale[3];             /* [Model] scale */
    float fov;                  /* [Camera] fov, degrees (40) */
    float near_z;               /* [Camera] near (0.1) */
    float far_z;                /* [Camera] far (10000) */
    float yshift;               /* [Camera] yshift */
    int localcoord_h;           /* [StageInfo] localcoord height */
} Stage3D;

extern Stage3D g_stage3d;

/* [Video] EnableModelShadow: the lights of models cast shadows (1, the default). */
extern int g_modelShadows;

Model *Model_Load(const char *path);
void Model_Free(Model *mdl);
void Model_Step(Model *mdl);
void Model_Reset(Model *mdl);
void Model_EnableAnim(Model *mdl, int index, int enabled);
int Model_LoadEnvironment(Model *mdl, const char *path, float intensity);
void Model_DrawShadows(Model *mdl);
void Model_Draw(Model *mdl, int scene, int layer, int default_layer, const float *proj, const float *view,
                const ModelFx *fx, float outline, const float *camera);

Model *Bg3D_LoadModel(const char *file, const char *dir);
void Bg3D_SetModel(Model *mdl);
void Bg3D_EndModel(void);
void Bg3D_LoadMotifModel(Ini *ini);
Model *Bg3D_MotifModel(void);
void Bg3D_FreeMotifModel(void);
void Bg3D_Attach(const Bg *bg, Ini *ini);
void Bg3D_Detach(const Bg *bg);
void Bg3D_Tick(const Bg *bg);
void Bg3D_Draw(const Bg *bg, int layer);

void Stage3D_Load(Stage *st, Ini *ini, const char *path);
void Stage3D_Free(void);
void Stage3D_Tick(void);
void Stage3D_Reset(void);
void Stage3D_Draw(int layer);

#endif /* MUGEN_MODEL_H */
