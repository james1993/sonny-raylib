#include "glow.h"

#include <stddef.h>

#include "render.h"
#include "rlgl.h"

/* The stage's own size; the targets are full-size so the silhouette can be
   drawn where it belongs and laid back down without any coordinate juggling.
   In pixels they are as big as the stage is being drawn at, so the halo is as
   fine as the figure it comes off. */
#define GLOW_W 800
#define GLOW_H 575

/* A box blur, one pass, as Flash's quality-of-one filters use. `step` carries
   the direction, so the same shader does both halves of the separable pair.
   Only the alpha matters: the silhouette is what is being blurred, and the
   colour is thrown away. */
static const char *BLUR_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec2 step;\n"
    "uniform float radius;\n"
    "void main() {\n"
    "    float total = 0.0;\n"
    "    float weight = 0.0;\n"
    "    int n = int(radius);\n"
    "    for (int i = -n; i <= n; i++) {\n"
    "        total += texture(texture0, fragTexCoord\n"
    "                         + step * float(i)).a;\n"
    "        weight += 1.0;\n"
    "    }\n"
    "    finalColor = vec4(1.0, 1.0, 1.0, total / weight);\n"
    "}\n";

/* The silhouette in one flat colour: the shape's alpha, the tint's colour.
   The captured figure still carries the model's own colours and they are
   never wanted -- the white fill throws them away, and the halo and the rim
   are the glow's colour, not the model's. */
static const char *FILL_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "void main() {\n"
    "    finalColor = vec4(fragColor.rgb,\n"
    "                      fragColor.a * texture(texture0, fragTexCoord).a);\n"
    "}\n";

/* The rim inside the outline: an inner glow's alpha is the blurred inverse of
   the shape, which is one minus the blurred shape, kept inside the shape
   itself. texture1 is the blurred silhouette, texture0 the sharp one. */
static const char *RIM_FS =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform sampler2D texture1;\n"
    "void main() {\n"
    "    float sharp = texture(texture0, fragTexCoord).a;\n"
    "    float soft = texture(texture1, fragTexCoord).a;\n"
    "    finalColor = vec4(fragColor.rgb,\n"
    "                      fragColor.a * clamp(1.0 - soft, 0.0, 1.0)\n"
    "                      * sharp);\n"
    "}\n";

static RenderTexture2D shape;      /* the silhouette as drawn */
static RenderTexture2D pass1;      /* blurred across */
static RenderTexture2D pass2;      /* and then down */
static Shader blur;
static Shader rim_shader;
static Shader fill_shader;
static int blur_step, blur_radius, rim_second;
static int ready;                  /* 1 loaded, -1 tried and failed */
static int capturing;
static int target_w, target_h;     /* the targets' size in pixels */

static int glow_load(void)
{
    if (ready)
        return ready > 0;
    ready = -1;
    target_w = (int)(GLOW_W * render_scale() + 0.5f);
    target_h = (int)(GLOW_H * render_scale() + 0.5f);
    shape = LoadRenderTexture(target_w, target_h);
    pass1 = LoadRenderTexture(target_w, target_h);
    pass2 = LoadRenderTexture(target_w, target_h);
    blur = LoadShaderFromMemory(NULL, BLUR_FS);
    rim_shader = LoadShaderFromMemory(NULL, RIM_FS);
    fill_shader = LoadShaderFromMemory(NULL, FILL_FS);
    if (shape.id == 0 || pass1.id == 0 || pass2.id == 0
        || blur.id == 0 || rim_shader.id == 0 || fill_shader.id == 0)
        return 0;
    blur_step = GetShaderLocation(blur, "step");
    blur_radius = GetShaderLocation(blur, "radius");
    rim_second = GetShaderLocation(rim_shader, "texture1");
    /* raylib hands back its own default shader when one of these will not
       compile, so a missing uniform is how a failure shows up. */
    if (blur_step < 0 || blur_radius < 0 || rim_second < 0)
        return 0;
    /* The blurred silhouette is sampled well outside the figure, and a
       clamped edge would smear its last row all the way out. */
    SetTextureWrap(shape.texture, TEXTURE_WRAP_CLAMP);
    SetTextureFilter(shape.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(pass1.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(pass2.texture, TEXTURE_FILTER_BILINEAR);
    ready = 1;
    return 1;
}

/* Everything here is alpha, and the alpha has to survive being written.
   raylib's ordinary blend runs the destination's alpha through the source's
   alpha as well, so drawing something with alpha a onto a cleared target
   stores a squared -- and a silhouette blurred twice through that comes out
   at the fourth power of itself, which is why an edge that should be half
   lit reads as a tenth. main.c sets the separate factors the whole game
   draws with; this asks for them, because none of this happens inside the
   pass that turns them on. */
static void glow_blend_begin(void)
{
    BeginBlendMode(BLEND_CUSTOM_SEPARATE);
}

static void glow_blend_end(void)
{
    EndBlendMode();
}

int glow_capture_begin(void)
{
    if (!glow_load())
        return 0;
    BeginTextureMode(shape);
    render_stage_projection();
    ClearBackground(BLANK);
    glow_blend_begin();
    capturing = 1;
    return 1;
}

void glow_capture_end(void)
{
    if (!capturing)
        return;
    capturing = 0;
    glow_blend_end();
    EndTextureMode();
}

/* A render target is drawn upside down, because its texture is written the
   way OpenGL writes one. `size` is how big to lay it down: the blur passes
   draw one target over the whole of another and work in texels, and composing
   draws into the stage and works in its units. */
static void blit(Texture2D tex, Color tint, float w, float h)
{
    Rectangle src = {0, 0, (float)tex.width, -(float)tex.height};
    Rectangle dst = {0, 0, w, h};
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0.0f, tint);
}

void glow_blur(float width)
{
    if (ready <= 0)
        return;
    /* Flash's blurX is how wide the blur is, not how far it reaches, so the
       kernel runs half that either side of the pixel. Taking it as a reach
       makes the glow twice as wide and half as strong, which reads as a grey
       smudge rather than an edge. */
    /* The kernel walks the target's own texels, and there is more than one of
       them to a stage pixel when the stage is drawn large. */
    float radius = width * render_scale() / 2.0f;
    if (radius < 1.0f)
        radius = 1.0f;

    /* Across, then down. Both passes run into their own target, so neither
       reads what it is writing. */
    float across[2] = {1.0f / target_w, 0.0f};
    float down[2] = {0.0f, 1.0f / target_h};
    SetShaderValue(blur, blur_radius, &radius, SHADER_UNIFORM_FLOAT);

    BeginTextureMode(pass1);
    ClearBackground(BLANK);
    glow_blend_begin();
    SetShaderValue(blur, blur_step, across, SHADER_UNIFORM_VEC2);
    BeginShaderMode(blur);
    blit(shape.texture, WHITE, (float)target_w, (float)target_h);
    EndShaderMode();
    glow_blend_end();
    EndTextureMode();

    BeginTextureMode(pass2);
    ClearBackground(BLANK);
    glow_blend_begin();
    SetShaderValue(blur, blur_step, down, SHADER_UNIFORM_VEC2);
    BeginShaderMode(blur);
    blit(pass1.texture, WHITE, (float)target_w, (float)target_h);
    EndShaderMode();
    glow_blend_end();
    EndTextureMode();
}

/* Laying it down is separate from working it out: the passes above have to
   happen before the frame's own target is open, and this after. */
void glow_compose(Color rim)
{
    if (ready <= 0)
        return;
    /* The halo outside, from the blurred silhouette at full strength. */
    BeginShaderMode(fill_shader);
    blit(pass2.texture, rim, GLOW_W, GLOW_H);
    /* The figure itself, filled flat -- the hundred-pixel inner glow
       saturates, so nothing of its own colour is left. */
    blit(shape.texture, WHITE, GLOW_W, GLOW_H);
    EndShaderMode();
    /* And the rim just inside the outline. The blurred silhouette goes in as
       a second sampler, bound inside the shader block: beginning one flushes
       the batch, which is where rlgl hands the extra units over, so binding
       it beforehand loses it. */
    BeginShaderMode(rim_shader);
    SetShaderValueTexture(rim_shader, rim_second, pass2.texture);
    blit(shape.texture, rim, GLOW_W, GLOW_H);
    EndShaderMode();
}

void glow_unload(void)
{
    if (ready <= 0)
        return;
    UnloadRenderTexture(shape);
    UnloadRenderTexture(pass1);
    UnloadRenderTexture(pass2);
    UnloadShader(blur);
    UnloadShader(rim_shader);
    UnloadShader(fill_shader);
    ready = 0;
}
