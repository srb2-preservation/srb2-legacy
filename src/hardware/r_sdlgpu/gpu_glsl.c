// SRB2 LEGACY
//-----------------------------------------------------------------------------
// Copyright (C) 2026 by SRB2 Preservation.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  gpu_glsl.c
/// \brief Translates the game's OpenGL shaders for the SDL GPU renderer
///
/// The shaders of the game and of its addons are written for OpenGL's
/// compatibility profile: gl_Vertex, gl_FragColor, texture2D, loose uniforms...
/// None of that exists in the GLSL that compiles to SPIR-V, so the source is
/// rewritten: the old names are replaced with ones declared in a header put
/// in front of the shader, the uniforms the game provides are taken out (they
/// are in the header's uniform block instead), varyings are given locations,
/// and main() is wrapped to set up the inputs and finish the outputs.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gpu_glsl.h"

#ifdef HAVE_SHADERC
#include <shaderc/shaderc.h>
#endif

#ifdef HAVE_SPIRV_CROSS
#include <spirv_cross_c.h>
#endif

// ==========================================================================
//                                                             STRING BUILDER
// ==========================================================================

typedef struct
{
	char *buf;
	size_t len, cap;
	int failed;
} sb_t;

static void SB_Add(sb_t *sb, const char *text, size_t len)
{
	if (sb->failed)
		return;

	if (sb->len + len + 1 > sb->cap)
	{
		size_t newcap = sb->cap ? sb->cap * 2 : 4096;
		char *newbuf;
		while (newcap < sb->len + len + 1)
			newcap *= 2;
		newbuf = realloc(sb->buf, newcap);
		if (!newbuf)
		{
			sb->failed = 1;
			return;
		}
		sb->buf = newbuf;
		sb->cap = newcap;
	}

	memcpy(sb->buf + sb->len, text, len);
	sb->len += len;
	sb->buf[sb->len] = '\0';
}

static void SB_Puts(sb_t *sb, const char *text)
{
	SB_Add(sb, text, strlen(text));
}

// ==========================================================================
//                                                                      NAMES
// ==========================================================================

typedef struct
{
	const char *from;
	const char *to;
} rename_t;

// in both stages
static const rename_t renames_common[] =
{
	{"main", "srb2_main"},
	{"texture1D", "srb2_texture1D"},
	{"texture2D", "texture"},
	{"texture3D", "texture"},
	{"textureCube", "texture"},
	{"texture2DLod", "textureLod"},
	{"texture3DLod", "textureLod"},
	{"texture2DProj", "textureProj"},
	{"sampler1D", "sampler2D"},
	{"gl_ModelViewMatrix", "srb2_ModelViewMatrix"},
	{"gl_ProjectionMatrix", "srb2_ProjectionMatrix"},
	{"gl_ModelViewProjectionMatrix", "srb2_ModelViewProjectionMatrix"},
	{"gl_NormalMatrix", "srb2_NormalMatrix"},
	{"gl_TexCoord", "srb2_TexCoord"},
	{NULL, NULL}
};

static const rename_t renames_vertex[] =
{
	{"gl_Vertex", "srb2_Vertex"},
	{"gl_Normal", "srb2_Normal"},
	{"gl_Color", "srb2_Color"},
	{"gl_MultiTexCoord0", "srb2_MultiTexCoord0"},
	{"gl_FrontColor", "srb2_FrontColor"},
	{"gl_BackColor", "srb2_BackColor"},
	{"gl_ClipVertex", "srb2_ClipVertex"},
	{"ftransform", "srb2_ftransform"},
	{NULL, NULL}
};

static const rename_t renames_fragment[] =
{
	{"gl_Color", "srb2_FrontColor"},
	{"gl_FragColor", "srb2_FragColor"},
	{"gl_FragData", "srb2_FragData"},
	{"gl_FragCoord", "srb2_FragCoord"},
	{NULL, NULL}
};

// uniforms the game provides: they are in the header's uniform block
static const char *const known_uniforms[] =
{
	"poly_color", "tint_color", "fade_color",
	"lighting", "fade_start", "fade_end",
	"leveltime", "scr_resolution",
	NULL
};

// textures the game provides: they are declared in the header
static const char *const known_samplers[] =
{
	"tex", "palette_lookup_tex", "lighttable_tex", "palette_tex",
	NULL
};

// qualifiers that can come before or after uniform, varying, in, out...
static const char *const qualifiers[] =
{
	"flat", "smooth", "noperspective", "centroid", "invariant", "precise",
	"highp", "mediump", "lowp",
	NULL
};

static int InList(const char *const *list, const char *name, size_t len)
{
	for (; *list; list++)
		if (strlen(*list) == len && !strncmp(*list, name, len))
			return 1;
	return 0;
}

static const char *Rename(const rename_t *table, const char *name, size_t len)
{
	for (; table->from; table++)
		if (strlen(table->from) == len && !strncmp(table->from, name, len))
			return table->to;
	return NULL;
}

static int IsIdentStart(char c)
{
	return isalpha((unsigned char)c) || c == '_';
}

static int IsIdentChar(char c)
{
	return isalnum((unsigned char)c) || c == '_';
}

// ==========================================================================
//                                                                   VARYINGS
// ==========================================================================

// Varyings are matched between the stages by location in SPIR-V, not by name.
// Locations 0 and 1 are the colour and the texture coordinates.
#define FIRST_VARYING_LOCATION 2
#define MAX_VARYINGS 32

// Both stages end up with all varyings, in the same order, even those only one of
// them declares or uses: Direct3D 12 wants the outputs of the vertex shader to
// match the inputs of the pixel shader.
typedef struct
{
	char name[64];
	int location;
	char type[32];
	char quals[128];
	int arraysize; // 0 if it's not an array
	int declared[2]; // by stage
} varying_t;

typedef struct
{
	varying_t list[MAX_VARYINGS];
	int count;
	int nextlocation;
} varyings_t;

static int Varying_Location(varyings_t *varyings, gpu_stage_t stage, const char *name, size_t len, int slots,
	const char *type, size_t typelen, const char *quals, int arraysize)
{
	varying_t *varying;
	int i;

	if (len >= sizeof (varyings->list[0].name))
		len = sizeof (varyings->list[0].name) - 1;

	for (i = 0; i < varyings->count; i++)
		if (strlen(varyings->list[i].name) == len && !strncmp(varyings->list[i].name, name, len))
		{
			varyings->list[i].declared[stage] = 1;
			return varyings->list[i].location;
		}

	if (varyings->count == MAX_VARYINGS || typelen >= sizeof (varyings->list[0].type))
		return -1;

	varying = &varyings->list[varyings->count++];
	memcpy(varying->name, name, len);
	varying->name[len] = '\0';
	memcpy(varying->type, type, typelen);
	varying->type[typelen] = '\0';
	snprintf(varying->quals, sizeof (varying->quals), "%s", quals);
	varying->arraysize = arraysize;
	varying->declared[stage] = 1;
	varying->location = varyings->nextlocation;
	varyings->nextlocation += slots;
	return varying->location;
}

// The name a varying has in a stage that doesn't declare it
static void Varying_Name(const varying_t *varying, gpu_stage_t stage, char *name, size_t size)
{
	if (varying->declared[stage])
		snprintf(name, size, "%s", varying->name);
	else
		snprintf(name, size, "srb2_link_%d", varying->location);
}

// Declares, in a stage that doesn't declare them, the varyings the other one does
static void Varyings_DeclareMissing(sb_t *out, const varyings_t *varyings, gpu_stage_t stage)
{
	int i;
	char line[300], name[80], array[24];

	for (i = 0; i < varyings->count; i++)
	{
		const varying_t *varying = &varyings->list[i];
		if (varying->declared[stage])
			continue;
		Varying_Name(varying, stage, name, sizeof (name));
		if (varying->arraysize)
			snprintf(array, sizeof (array), "[%d]", varying->arraysize);
		else
			array[0] = '\0';
		snprintf(line, sizeof (line), "layout(location = %d) %s%s %s %s%s;\n", varying->location, varying->quals,
			(stage == GPU_STAGE_VERTEX) ? "out" : "in", varying->type, name, array);
		SB_Puts(out, line);
	}
}

// An expression that reads a varying, as a float. Empty if it can't.
static void Varying_Read(const varying_t *varying, gpu_stage_t stage, char *expression, size_t size)
{
	static const char *const vectors[] = {"vec", "ivec", "uvec", "bvec", "dvec", NULL};
	static const char *const scalars[] = {"float", "int", "uint", "bool", "double", NULL};
	const char *const *prefix;
	char name[80];
	const char *component = NULL;

	Varying_Name(varying, stage, name, sizeof (name));

	for (prefix = scalars; *prefix && !component; prefix++)
		if (!strcmp(varying->type, *prefix))
			component = "";
	for (prefix = vectors; *prefix && !component; prefix++)
		if (!strncmp(varying->type, *prefix, strlen(*prefix)))
			component = ".x";
	if (!component && !strncmp(varying->type, "mat", 3))
		component = "[0][0]";

	if (component)
		snprintf(expression, size, "float(%s%s%s)", name, varying->arraysize ? "[0]" : "", component);
	else
		expression[0] = '\0';
}

// how many locations a type takes
static int Type_Slots(const char *type, size_t len)
{
	if (len >= 4 && !strncmp(type, "mat", 3) && isdigit((unsigned char)type[3]))
		return type[3] - '0';
	return 1;
}

// if a value of the type can be made from a single number
static int Type_IsNumeric(const char *type, size_t len)
{
	static const char *const prefixes[] = {"float", "int", "uint", "bool", "vec", "ivec", "uvec", "bvec", "mat", NULL};
	const char *const *prefix;

	for (prefix = prefixes; *prefix; prefix++)
	{
		const size_t prefixlen = strlen(*prefix);
		if (len >= prefixlen && !strncmp(type, *prefix, prefixlen)
			&& (len == prefixlen || isdigit((unsigned char)type[prefixlen])))
			return 1;
	}
	return 0;
}

// ==========================================================================
//                                                                TRANSLATION
// ==========================================================================

typedef struct
{
	gpu_stage_t stage;
	varyings_t *varyings;
	sb_t *out;
	char fragment_output[64]; // an output the fragment shader declared itself
	int uses_fragdata;
	char *error;
	size_t errorsize;
} translator_t;

// skips spaces and comments, returns where the next token starts
static const char *SkipSpace(const char *p)
{
	for (;;)
	{
		if (isspace((unsigned char)*p))
			p++;
		else if (p[0] == '/' && p[1] == '/')
		{
			while (*p && *p != '\n')
				p++;
		}
		else if (p[0] == '/' && p[1] == '*')
		{
			p += 2;
			while (*p && !(p[0] == '*' && p[1] == '/'))
				p++;
			if (*p)
				p += 2;
		}
		else
			return p;
	}
}

static void TrimSpan(const char **start, const char **end)
{
	while (*start < *end && isspace((unsigned char)**start))
		(*start)++;
	while (*end > *start && isspace((unsigned char)(*end)[-1]))
		(*end)--;
}

// Handles a declaration of the shader's interface at the global scope:
// uniform, varying, attribute, in, out. p is at the first word of a statement.
// Returns where the statement ends, or NULL if it's not such a declaration,
// in which case nothing was written.
static const char *Translate_Interface(translator_t *t, const char *p)
{
	const char *start = p;
	const char *kind = NULL, *type = NULL, *end, *decl;
	size_t kindlen = 0, typelen = 0;
	char quals[128] = "";
	int is_uniform, is_varying, is_output;
	int newlines = 0;
	sb_t *out = t->out;

	// qualifiers, then what kind of declaration this is
	for (;;)
	{
		const char *word;
		size_t len;

		p = SkipSpace(p);
		if (!IsIdentStart(*p))
			return NULL;

		word = p;
		while (IsIdentChar(*p))
			p++;
		len = p - word;

		if (len == 6 && !strncmp(word, "layout", 6))
		{
			// the locations are ours to give
			int depth = 0;
			p = SkipSpace(p);
			if (*p != '(')
				return NULL;
			do
			{
				if (*p == '(') depth++;
				else if (*p == ')') depth--;
				else if (!*p) return NULL;
				p++;
			} while (depth);
		}
		else if (InList(qualifiers, word, len))
		{
			if (strlen(quals) + len + 2 < sizeof (quals))
			{
				strncat(quals, word, len);
				strcat(quals, " ");
			}
		}
		else if ((len == 7 && !strncmp(word, "uniform", 7))
			|| (len == 7 && !strncmp(word, "varying", 7))
			|| (len == 9 && !strncmp(word, "attribute", 9))
			|| (len == 2 && !strncmp(word, "in", 2))
			|| (len == 3 && !strncmp(word, "out", 3)))
		{
			kind = word;
			kindlen = len;
			break;
		}
		else
			return NULL;
	}

	// more qualifiers, then the type
	for (;;)
	{
		const char *word;
		size_t len;

		p = SkipSpace(p);
		if (!IsIdentStart(*p))
			return NULL;

		word = p;
		while (IsIdentChar(*p))
			p++;
		len = p - word;

		if (InList(qualifiers, word, len))
		{
			if (strlen(quals) + len + 2 < sizeof (quals))
			{
				strncat(quals, word, len);
				strcat(quals, " ");
			}
		}
		else
		{
			type = word;
			typelen = len;
			break;
		}
	}

	// find the end of the statement. A block ("uniform Name { ... }") is left alone.
	for (end = p; *end != ';'; end++)
		if (!*end || *end == '{')
			return NULL;

	for (decl = start; decl < end; decl++)
		if (*decl == '\n')
			newlines++;

	is_uniform = (kindlen == 7 && kind[0] == 'u');
	is_varying = (kindlen == 7 && kind[0] == 'v')
		|| (t->stage == GPU_STAGE_VERTEX && kindlen == 3)
		|| (t->stage == GPU_STAGE_FRAGMENT && kindlen == 2);
	is_output = (t->stage == GPU_STAGE_FRAGMENT && kindlen == 3);

	// each of the names declared
	decl = p;
	while (decl < end)
	{
		const char *declend = decl, *name, *nameend, *rest;
		int depth = 0;
		int slots = Type_Slots(type, typelen);

		while (declend < end && (depth || *declend != ','))
		{
			if (*declend == '(' || *declend == '[') depth++;
			else if (*declend == ')' || *declend == ']') depth--;
			declend++;
		}

		name = SkipSpace(decl);
		nameend = name;
		while (nameend < declend && IsIdentChar(*nameend))
			nameend++;
		rest = nameend; // an array size, an initial value
		{
			const char *reststart = rest, *restend = declend;
			const char *bracket;
			TrimSpan(&reststart, &restend);
			bracket = memchr(reststart, '[', restend - reststart);
			if (bracket)
			{
				int size = atoi(bracket + 1);
				slots *= (size > 0) ? size : 4;
			}
		}

		if (nameend > name)
		{
			size_t namelen = nameend - name;
			int drop = 0;

			if (is_uniform)
			{
				if (typelen >= 7 && !strncmp(type, "sampler", 7))
				{
					// only the textures the game provides exist
					drop = 1;
					if (!InList(known_samplers, name, namelen) && t->error && !t->error[0])
						snprintf(t->error, t->errorsize, "the texture \"%.*s\" is not one the game provides", (int)namelen, name);
				}
				else if (InList(known_uniforms, name, namelen))
					drop = 1;
				// a uniform the game doesn't know about was never set by the
				// OpenGL renderer either: it becomes a plain variable
			}
			else if (is_varying)
			{
				int arraysize = 0;
				const char *bracket = memchr(rest, '[', declend - rest);
				int location;
				if (bracket)
					arraysize = atoi(bracket + 1);
				location = Varying_Location(t->varyings, t->stage, name, namelen, slots,
					type, typelen, quals, arraysize);
				char layout[200];

				if (location < 0)
				{
					if (t->error && !t->error[0])
						snprintf(t->error, t->errorsize, "too many varyings");
					drop = 1;
				}
				else
				{
					snprintf(layout, sizeof (layout), "layout(location = %d) %s%s ", location, quals,
						(t->stage == GPU_STAGE_VERTEX) ? "out" : "in");
					SB_Puts(out, layout);
				}
			}
			else if (is_output)
			{
				// copied to the real output by the wrapper around main()
				if (!t->fragment_output[0] && namelen < sizeof (t->fragment_output))
				{
					memcpy(t->fragment_output, name, namelen);
					t->fragment_output[namelen] = '\0';
				}
			}
			// attributes of the vertex shader: the game has none but the
			// built-in ones, so these become plain variables too

			if (!drop)
			{
				SB_Add(out, type, typelen);
				SB_Puts(out, " ");
				SB_Add(out, name, namelen);
				SB_Add(out, rest, declend - rest);
				// What OpenGL never sets is zero. A plain variable would be anything.
				if (!is_varying && !memchr(rest, '=', declend - rest) && !memchr(rest, '[', declend - rest)
					&& Type_IsNumeric(type, typelen))
				{
					SB_Puts(out, " = ");
					SB_Add(out, type, typelen);
					SB_Puts(out, "(0)");
				}
				SB_Puts(out, "; ");
			}
		}

		decl = (declend < end) ? declend + 1 : end;
	}

	// keep the line numbers of the original
	while (newlines--)
		SB_Puts(out, "\n");

	return end + 1;
}

static void Translate_Body(translator_t *t, const char *src)
{
	const rename_t *stage_renames = (t->stage == GPU_STAGE_VERTEX) ? renames_vertex : renames_fragment;
	sb_t *out = t->out;
	const char *p = src;
	int depth = 0;
	int statement_start = 1; // at the first word of a statement
	int line_start = 1; // only spaces so far on this line
	int in_directive = 0;
	int saved_statement_start = 1;

	while (*p)
	{
		if (*p == '\n')
		{
			if (in_directive && (p == src || p[-1] != '\\'))
			{
				in_directive = 0;
				statement_start = saved_statement_start;
			}
			SB_Add(out, p++, 1);
			line_start = 1;
		}
		else if (isspace((unsigned char)*p))
			SB_Add(out, p++, 1);
		else if ((p[0] == '/' && p[1] == '/') || (p[0] == '/' && p[1] == '*'))
		{
			// comments are kept, for the line numbers
			const char *end = p;
			if (p[1] == '/')
			{
				while (*end && *end != '\n')
					end++;
			}
			else
			{
				end += 2;
				while (*end && !(end[0] == '*' && end[1] == '/'))
					end++;
				if (*end)
					end += 2;
			}
			SB_Add(out, p, end - p);
			p = end;
		}
		else if (*p == '#' && line_start)
		{
			const char *word = p + 1;
			while (*word == ' ' || *word == '\t')
				word++;

			if (!strncmp(word, "version", 7) && !IsIdentChar(word[7]))
			{
				// the version is ours to choose
				while (*p && *p != '\n')
					p++;
			}
			else
			{
				in_directive = 1;
				saved_statement_start = statement_start;
				SB_Add(out, p++, 1);
			}
			line_start = 0;
		}
		else if (IsIdentStart(*p))
		{
			const char *word = p, *to;
			size_t len;

			line_start = 0;

			if (!depth && statement_start && !in_directive)
			{
				const char *end = Translate_Interface(t, p);
				if (end)
				{
					p = end;
					continue;
				}
			}

			while (IsIdentChar(*p))
				p++;
			len = p - word;

			if (t->stage == GPU_STAGE_FRAGMENT && len == 11 && !strncmp(word, "gl_FragData", 11))
				t->uses_fragdata = 1;

			to = Rename(renames_common, word, len);
			if (!to)
				to = Rename(stage_renames, word, len);

			if (to)
				SB_Puts(out, to);
			else
				SB_Add(out, word, len);

			if (!in_directive)
				statement_start = 0;
		}
		else if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1])))
		{
			// a number, which can have letters in it
			const char *end = p;
			while (IsIdentChar(*end) || *end == '.'
				|| ((*end == '+' || *end == '-') && (end[-1] == 'e' || end[-1] == 'E')))
				end++;
			SB_Add(out, p, end - p);
			p = end;
			line_start = 0;
			if (!in_directive)
				statement_start = 0;
		}
		else
		{
			if (!in_directive)
			{
				if (*p == '{')
				{
					depth++;
					statement_start = 0;
				}
				else if (*p == '}')
				{
					if (depth)
						depth--;
					statement_start = !depth;
				}
				else if (*p == ';')
					statement_start = 1;
				else
					statement_start = 0;
			}
			line_start = 0;
			SB_Add(out, p++, 1);
		}
	}

	SB_Puts(out, "\n");
}

// --------------------------------------------------------------------------
// What goes before and after the shader
// --------------------------------------------------------------------------

// must match gpu_uniforms_t
#define UNIFORM_BLOCK(set) \
	"layout(set = " set ", binding = 0, std140) uniform SRB2Uniforms\n" \
	"{\n" \
	"\tmat4 srb2_ProjectionMatrix;\n" \
	"\tmat4 srb2_ModelViewMatrix;\n" \
	"\tvec4 poly_color;\n" \
	"\tvec4 tint_color;\n" \
	"\tvec4 fade_color;\n" \
	"\tvec4 srb2_CurrentColor;\n" \
	"\tfloat lighting;\n" \
	"\tfloat fade_start;\n" \
	"\tfloat fade_end;\n" \
	"\tfloat leveltime;\n" \
	"\tvec2 scr_resolution;\n" \
	"\tfloat srb2_AlphaThreshold;\n" \
	"\tfloat srb2_Lerp;\n" \
	"\tvec2 srb2_TargetSize;\n" \
	"};\n" \
	"#define srb2_ModelViewProjectionMatrix (srb2_ProjectionMatrix * srb2_ModelViewMatrix)\n" \
	"#define srb2_NormalMatrix mat3(srb2_ModelViewMatrix)\n"

static const char vertex_header[] =
	"#version 450\n"
	UNIFORM_BLOCK("1")
	"layout(location = 0) in vec3 srb2_in_Position;\n"
	"layout(location = 1) in vec2 srb2_in_TexCoord;\n"
	"layout(location = 2) in vec4 srb2_in_Color;\n"
	"layout(location = 3) in vec3 srb2_in_Normal;\n"
	"layout(location = 4) in vec3 srb2_in_NextPosition;\n"
	"layout(location = 5) in vec3 srb2_in_NextNormal;\n"
	"layout(location = 0) out vec4 srb2_FrontColor;\n"
	"layout(location = 1) out vec4 srb2_TexCoord[1];\n"
	"vec4 srb2_Vertex;\n"
	"vec3 srb2_Normal;\n"
	"vec4 srb2_Color;\n"
	"vec4 srb2_MultiTexCoord0;\n"
	"vec4 srb2_BackColor;\n"
	"vec4 srb2_ClipVertex;\n"
	"vec4 srb2_ftransform() { return srb2_ProjectionMatrix * srb2_ModelViewMatrix * srb2_Vertex; }\n"
	"#line 1\n";

static const char vertex_footer_start[] =
	"void main()\n"
	"{\n"
	"\tsrb2_Vertex = vec4(mix(srb2_in_Position, srb2_in_NextPosition, srb2_Lerp), 1.0);\n"
	"\tsrb2_Normal = mix(srb2_in_Normal, srb2_in_NextNormal, srb2_Lerp);\n"
	"\tsrb2_Color = srb2_in_Color * srb2_CurrentColor;\n"
	"\tsrb2_MultiTexCoord0 = vec4(srb2_in_TexCoord, 0.0, 1.0);\n"
	"\tsrb2_FrontColor = srb2_Color;\n"
	"\tsrb2_TexCoord[0] = srb2_MultiTexCoord0;\n";
	// then every varying is set to zero, so that none is left out

static const char vertex_footer_end[] =
	"\tsrb2_main();\n"
	// OpenGL's clip space has depth from -w to w, SDL GPU's from 0 to w
	"\tgl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n"
	"}\n";

static const char fragment_header[] =
	"#version 450\n"
	UNIFORM_BLOCK("3")
	"layout(set = 2, binding = 0) uniform sampler2D tex;\n"
	"layout(set = 2, binding = 1) uniform sampler3D palette_lookup_tex;\n"
	"layout(set = 2, binding = 2) uniform sampler2D lighttable_tex;\n"
	"layout(set = 2, binding = 3) uniform sampler2D palette_tex;\n"
	"layout(location = 0) in vec4 srb2_FrontColor;\n"
	"layout(location = 1) in vec4 srb2_TexCoord[1];\n"
	"layout(location = 0) out vec4 srb2_Output;\n"
	"vec4 srb2_FragColor;\n"
	"vec4 srb2_FragData[1];\n"
	"vec4 srb2_FragCoord;\n"
	// there are no 1D textures in SDL GPU: they are 2D textures with one row
	"vec4 srb2_texture1D(sampler2D s, float c) { return texture(s, vec2(c, 0.5)); }\n"
	"vec4 srb2_texture1D(sampler2D s, float c, float bias) { return texture(s, vec2(c, 0.5), bias); }\n"
	"#line 1\n";

static const char fragment_footer_start[] =
	"void main()\n"
	"{\n"
	// OpenGL counts the rows of the screen from the bottom
	"\tsrb2_FragCoord = vec4(gl_FragCoord.x, srb2_TargetSize.y - gl_FragCoord.y, gl_FragCoord.z, gl_FragCoord.w);\n"
	"\tsrb2_FragColor = vec4(0.0);\n"
	"\tsrb2_FragData[0] = vec4(0.0);\n"
	"\tsrb2_main();\n"
	"\tsrb2_Output = ";

static const char fragment_footer_end[] =
	";\n"
	// then every input is read, so that none is left out (see fragment_footer_keep)
	"";

static const char fragment_footer_test[] =
	// OpenGL's alpha test
	"\tif (srb2_Output.a <= srb2_AlphaThreshold)\n"
	"\t\tdiscard;\n"
	"}\n";

int GPU_TranslateGLSL(const char *vertex, const char *fragment,
	char **vertex_out, char **fragment_out, char *error, size_t errorsize)
{
	varyings_t varyings;
	translator_t t;
	sb_t vs = {NULL, 0, 0, 0}, fs = {NULL, 0, 0, 0};

	memset(&varyings, 0, sizeof (varyings));
	varyings.nextlocation = FIRST_VARYING_LOCATION;

	if (error && errorsize)
		error[0] = '\0';

	memset(&t, 0, sizeof (t));
	t.varyings = &varyings;
	t.error = error;
	t.errorsize = errorsize;

	t.stage = GPU_STAGE_VERTEX;
	t.out = &vs;
	SB_Puts(&vs, vertex_header);
	Translate_Body(&t, vertex);

	t.stage = GPU_STAGE_FRAGMENT;
	t.out = &fs;
	SB_Puts(&fs, fragment_header);
	Translate_Body(&t, fragment);

	// Now that both stages were seen, give them the same varyings.
	Varyings_DeclareMissing(&vs, &varyings, GPU_STAGE_VERTEX);
	Varyings_DeclareMissing(&fs, &varyings, GPU_STAGE_FRAGMENT);

	SB_Puts(&vs, vertex_footer_start);
	{
		int i;
		char line[300], name[80];
		for (i = 0; i < varyings.count; i++)
		{
			const varying_t *varying = &varyings.list[i];
			Varying_Name(varying, GPU_STAGE_VERTEX, name, sizeof (name));
			if (varying->arraysize)
				snprintf(line, sizeof (line), "\tfor (int srb2_i = 0; srb2_i < %d; srb2_i++) %s[srb2_i] = %s(0);\n",
					varying->arraysize, name, varying->type);
			else
				snprintf(line, sizeof (line), "\t%s = %s(0);\n", name, varying->type);
			SB_Puts(&vs, line);
		}
	}
	SB_Puts(&vs, vertex_footer_end);

	SB_Puts(&fs, fragment_footer_start);
	if (t.fragment_output[0])
		SB_Puts(&fs, t.fragment_output);
	else if (t.uses_fragdata)
		SB_Puts(&fs, "srb2_FragData[0]");
	else
		SB_Puts(&fs, "srb2_FragColor");
	SB_Puts(&fs, fragment_footer_end);
	{
		// Read every input in a branch that is never taken, but that the compiler can't know about.
		int i;
		char expression[200];
		SB_Puts(&fs, "\tif (srb2_AlphaThreshold == -12345.0)\n"
			"\t\tsrb2_Output.x += srb2_FrontColor.x + srb2_TexCoord[0].x + srb2_FragCoord.x");
		for (i = 0; i < varyings.count; i++)
		{
			Varying_Read(&varyings.list[i], GPU_STAGE_FRAGMENT, expression, sizeof (expression));
			if (expression[0])
			{
				SB_Puts(&fs, " + ");
				SB_Puts(&fs, expression);
			}
		}
		SB_Puts(&fs, ";\n");
	}
	SB_Puts(&fs, fragment_footer_test);

	if (vs.failed || fs.failed || !vs.buf || !fs.buf || (error && error[0]))
	{
		if (error && !error[0])
			snprintf(error, errorsize, "out of memory");
		free(vs.buf);
		free(fs.buf);
		return 0;
	}

	*vertex_out = vs.buf;
	*fragment_out = fs.buf;
	return 1;
}

unsigned long long GPU_HashShaderSources(const char *vertex, const char *fragment)
{
	// FNV-1a
	unsigned long long hash = 14695981039346656037ULL;
	const char *sources[2];
	int i;

	sources[0] = vertex;
	sources[1] = fragment;

	for (i = 0; i < 2; i++)
	{
		const unsigned char *p = (const unsigned char *)sources[i];
		do
		{
			hash ^= *p;
			hash *= 1099511628211ULL;
		} while (*p++); // the terminator too, to tell the two sources apart
	}

	return hash;
}

// ==========================================================================
//                                                                COMPILATION
// ==========================================================================

int GPU_CanCompileGLSL(void)
{
#ifdef HAVE_SHADERC
	return 1;
#else
	return 0;
#endif
}

void *GPU_CompileGLSL(const char *source, gpu_stage_t stage, const char *name, size_t *size, char **log)
{
#ifdef HAVE_SHADERC
	static shaderc_compiler_t compiler = NULL;
	shaderc_compile_options_t options;
	shaderc_compilation_result_t result;
	void *spirv = NULL;

	if (log)
		*log = NULL;

	if (!compiler)
		compiler = shaderc_compiler_initialize();
	if (!compiler)
		return NULL;

	options = shaderc_compile_options_initialize();
	if (!options)
		return NULL;

	shaderc_compile_options_set_source_language(options, shaderc_source_language_glsl);
	shaderc_compile_options_set_target_env(options, shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
	shaderc_compile_options_set_optimization_level(options, shaderc_optimization_level_performance);

	result = shaderc_compile_into_spv(compiler, source, strlen(source),
		(stage == GPU_STAGE_VERTEX) ? shaderc_glsl_vertex_shader : shaderc_glsl_fragment_shader,
		name, "main", options);

	shaderc_compile_options_release(options);

	if (!result)
		return NULL;

	if (log && shaderc_result_get_error_message(result)[0])
	{
		const char *message = shaderc_result_get_error_message(result);
		*log = malloc(strlen(message) + 1);
		if (*log)
			strcpy(*log, message);
	}

	if (shaderc_result_get_compilation_status(result) == shaderc_compilation_status_success)
	{
		*size = shaderc_result_get_length(result);
		spirv = malloc(*size);
		if (spirv)
			memcpy(spirv, shaderc_result_get_bytes(result), *size);
	}

	shaderc_result_release(result);
	return spirv;
#else
	(void)source;
	(void)stage;
	(void)name;
	(void)size;
	if (log)
		*log = NULL;
	return NULL;
#endif
}

// ==========================================================================
//                                          TRANSLATION FOR METAL AND D3D12
// ==========================================================================

int GPU_CanTranslateSPIRV(void)
{
#ifdef HAVE_SPIRV_CROSS
	return 1;
#else
	return 0;
#endif
}

char *GPU_TranslateSPIRV(const void *spirv, size_t size, int format, char *error, size_t errorsize)
{
#ifdef HAVE_SPIRV_CROSS
	spvc_context context = NULL;
	spvc_parsed_ir ir;
	spvc_compiler compiler;
	spvc_compiler_options options;
	const char *source = NULL;
	char *result = NULL;
	const int msl = (format == GPU_SHADERFORMAT_MSL);

	if (error && errorsize)
		error[0] = '\0';

	if (spvc_context_create(&context) != SPVC_SUCCESS)
	{
		if (error)
			snprintf(error, errorsize, "couldn't start SPIRV-Cross");
		return NULL;
	}

	if (spvc_context_parse_spirv(context, spirv, size / sizeof (SpvId), &ir) != SPVC_SUCCESS
		|| spvc_context_create_compiler(context, msl ? SPVC_BACKEND_MSL : SPVC_BACKEND_HLSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &compiler) != SPVC_SUCCESS
		|| spvc_compiler_create_compiler_options(compiler, &options) != SPVC_SUCCESS)
		goto fail;

	if (msl)
	{
		// What SDL GPU's Metal backend expects: a uniform buffer at [[buffer(n)]] and
		// a texture and its sampler at [[texture(n)]] and [[sampler(n)]], n being the
		// binding in the shader. Vertex buffers are at 14 and up, out of the way.
		spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_PLATFORM, SPVC_MSL_PLATFORM_MACOS);
		spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_MSL_VERSION, SPVC_MAKE_MSL_VERSION(2, 1, 0));
		spvc_compiler_options_set_bool(options, SPVC_COMPILER_OPTION_MSL_ENABLE_DECORATION_BINDING, SPVC_TRUE);
	}
	else
	{
		// What SDL GPU's Direct3D 12 backend expects: registers in the space of the
		// descriptor set (which follow SDL's rules for SPIR-V), and TEXCOORD<location> semantics.
		spvc_compiler_options_set_uint(options, SPVC_COMPILER_OPTION_HLSL_SHADER_MODEL, 60);
	}

	if (spvc_compiler_install_compiler_options(compiler, options) != SPVC_SUCCESS
		|| spvc_compiler_compile(compiler, &source) != SPVC_SUCCESS)
		goto fail;

	result = malloc(strlen(source) + 1);
	if (result)
		strcpy(result, source);
	spvc_context_destroy(context);
	return result;

fail:
	if (error)
		snprintf(error, errorsize, "%s", spvc_context_get_last_error_string(context));
	spvc_context_destroy(context);
	return NULL;
#else
	(void)spirv;
	(void)size;
	(void)format;
	if (error && errorsize)
		snprintf(error, errorsize, "this build has no SPIRV-Cross");
	return NULL;
#endif
}
