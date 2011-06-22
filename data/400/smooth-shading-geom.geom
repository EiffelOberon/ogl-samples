#version 400 core

#define ATTR_POSITION	0
#define ATTR_COLOR		3
#define ATTR_TEXCOORD	4
#define FRAG_COLOR		0

layout(triangle_strip, max_vertices = 4) out;
precision highp float;

in vec4 VertColor[];
layout(stream = 0) out vec4 GeomColor;

void main()
{
	for(int i = 0; i < gl_in.length(); ++i)
	{
		GeomColor = VertColor[i];
		gl_Position = gl_in[i].gl_Position;
		EmitVertex();
	}
	EndPrimitive();
}

