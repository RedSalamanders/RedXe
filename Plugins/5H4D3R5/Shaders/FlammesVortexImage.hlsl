// "Flammes 3 - Vortex" by athibaul - https://www.shadertoy.com/view/WsccDH
// 2D fake flame simulation using a simple vortex street, a fake combustion equation, and a chart for blackbody
// color. The author states no license of their own, so the shader is under Shadertoy's default terms:
// Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported (https://creativecommons.org/licenses/by-nc-sa/3.0/).
// HLSL port for RedXe: the Image pass. iChannel0 is this port's own feedback buffer, FlammesVortexBufferA.hlsl.
#include "ShadersCommon.hlsli"
#include "FlammesVortexCommon.hlsli"

vec3 colorFromTemperature( float t )
{
    // Convert a temperature in Kelvin to a color

    // Blackbody color data from Mitchell Charity's website
    // http://www.vendian.org/mncharity/dir3/blackbody/
    vec3 col = vec3(0,0,0);
    col = mix(col, rgb(0xff3800), clamp(t/1000.,0.,1.));
    col = mix(col, rgb(0xff8912), clamp((t-1000.)/1000.,0.,1.));
    // I'm unlikely to use higher temperatures for realistic flames,
    // but I included them anyway.
    col = mix(col, rgb(0xffb46b), clamp((t-2000.)/1000.,0.,1.));
    col = mix(col, rgb(0xffd1a3), clamp((t-3000.)/1000.,0.,1.));
    col = mix(col, rgb(0xffe4ce), clamp((t-4000.)/1000.,0.,1.));
    col = mix(col, rgb(0xfff3ef), clamp((t-5000.)/1000.,0.,1.));
    col = mix(col, rgb(0xf5f3ff), clamp((t-6000.)/1000.,0.,1.));
    return col*t/3000.;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = (2.*fragCoord - iResolution.xy)/iResolution.y;
    // The simulation is done horizontally, but let's make it vertical
    float heat = T0(uv.yx).b;
    // Constants are completely arbitrary
    float temperature = 1.1e5*heat;
    vec3 col = 1.5*colorFromTemperature(temperature);

    // Tone mapping and gamma correction
    col = mix(col, 1.-(4./27.)/(col*col), step(vec3(2./3.,2./3.,2./3.), col));
    //col = smoothstep(0.,1.,col);
    col = pow(col, vec3(1.0/2.2,1.0/2.2,1.0/2.2));
    fragColor = vec4(col,1.0);
}

#include "ShadersEntry.hlsli"
