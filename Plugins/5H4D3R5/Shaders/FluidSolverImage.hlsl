// "Fluid solver" by David A Roberts (davidar) - https://www.shadertoy.com/view/XlsBDf
// Single-pass Navier-Stokes solver, from "Simple and Fast Fluids" (https://hal.inria.fr/inria-00596050/document).
// The author states no license of their own, so the shader is under Shadertoy's default terms:
// Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported (https://creativecommons.org/licenses/by-nc-sa/3.0/).
// HLSL port for RedXe (Image pass). iChannel0 is this port's own feedback buffer, FluidSolverBufferA.hlsl.
#include "ShadersCommon.hlsli"

#define PI 3.141592653589793

void mainImage(out vec4 o, in vec2 p) {
    vec4 c = texture(iChannel0, p.xy / iResolution.xy);
    o.rgb = .6 + .6 * cos(6.3 * atan2(c.y,c.x)/(2.*PI) + vec3(0,23,21)); // velocity
    o.rgb *= c.w/5.; // ink
    o.rgb += clamp(c.z - 1., 0., 1.)/10.; // local fluid density
    o.a = 1.;
}

#include "ShadersEntry.hlsli"
