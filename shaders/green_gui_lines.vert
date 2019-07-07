#version 450

layout(push_constant) uniform Transformation
{
    vec2 position_a;
    vec2 position_b;
}
transformation;

void main()
{
    if(0 == gl_VertexIndex)
    {
        gl_Position = vec4(transformation.position_a, 0.0, 1.0);
    }
    else
    {
        gl_Position = vec4(transformation.position_b, 0.0, 1.0);
    }
}