// "Flammes 3 - Vortex" by athibaul - https://www.shadertoy.com/view/WsccDH
// 2D fake flame simulation using a simple vortex street, a fake combustion equation, and a chart for blackbody
// color. The author states no license of their own, so the shader is under Shadertoy's default terms:
// Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported (https://creativecommons.org/licenses/by-nc-sa/3.0/).
// HLSL port for RedXe: the Shadertoy "Common" tab, included by the Buffer A and Image passes.

#define T0(uv) texture(iChannel0, (0.5+0.5*(uv)*iResolution.y/iResolution.xy))

// Hash functions by Dave Hoskins
// https://www.shadertoy.com/view/4djSRW
//-------------------------------------
float hash11(float p)
{
    p = fract(p * .1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

vec2 hash21(float p)
{
    vec3 p3 = fract(vec3(p,p,p) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx+p3.yz)*p3.zy);

}

vec3 hash31(float p)
{
   vec3 p3 = fract(vec3(p,p,p) * vec3(.1031, .1030, .0973));
   p3 += dot(p3, p3.yzx+33.33);
   return fract((p3.xxy+p3.yzz)*p3.zyx);
}

vec4 hash41(float p)
{
    vec4 p4 = fract(vec4(p,p,p,p) * vec4(.1031, .1030, .0973, .1099));
    p4 += dot(p4, p4.wzxy+33.33);
    return fract((p4.xxyz+p4.yzzw)*p4.zywx);
}
//----------------------------------------------



// Convert sRGB-int8 to linear RGB-float
vec3 rgb(int r, int g, int b)
{
    return pow(vec3(r,g,b)/255., vec3(2.2,2.2,2.2));
}
vec3 rgb(int a)
{
    int r = (a>>16) & 0xff;
    int g = (a>>8) & 0xff;
    int b = a & 0xff;
    return rgb(r,g,b);
}
