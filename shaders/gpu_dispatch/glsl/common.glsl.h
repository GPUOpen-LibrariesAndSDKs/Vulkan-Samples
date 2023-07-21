//  Copyright (c) 2023 Advanced Micro Devices, Inc. All Rights Reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.

//  Additionally, this file contains sources with the following copyright:

// Simplex 3D noise implementation from: https://www.shadertoy.com/view/XsX3zB
// Copyright (c) 2013 Nikita Miropolskiy (MIT License)

// Gradient 2D noise implementation from: https://www.shadertoy.com/view/XdXBRH
// Copyright (c) 2017 Inigo Quilez (MIT License)


// 16 colors
const vec4[] COLOR_PALETTE = vec4[](
	vec4(1.00000000, 1.00000000, 1.00000000, 1.0),
	vec4(0.98431373, 0.95294118, 0.01960784, 1.0),
	vec4(1.00000000, 0.39215686, 0.01176471, 1.0),
	vec4(0.86666667, 0.03529412, 0.02745098, 1.0),
	vec4(0.94901961, 0.03137255, 0.51764706, 1.0),
	vec4(0.27843137, 0.00000000, 0.64705882, 1.0),
	vec4(0.00000000, 0.00000000, 0.82745098, 1.0),
	vec4(0.00784314, 0.67058824, 0.91764706, 1.0),
	vec4(0.12156863, 0.71764706, 0.07843137, 1.0),
	vec4(0.00000000, 0.39215686, 0.07058824, 1.0),
	vec4(0.33725490, 0.17254902, 0.01960784, 1.0),
	vec4(0.56470588, 0.44313725, 0.22745098, 1.0),
	vec4(0.75294118, 0.75294118, 0.75294118, 1.0),
	vec4(0.50196078, 0.50196078, 0.50196078, 1.0),
	vec4(0.25098039, 0.25098039, 0.25098039, 1.0),
	vec4(0.00000000, 0.00000000, 0.00000000, 1.0)
);

// 128 colors
const vec4[] MATERIAL_MAP_COLORS = vec4[](
	vec4(0.00000000, 0.00000000, 0.00000000, 1.0),
	vec4(0.26666667, 0.26666667, 0.00000000, 1.0),
	vec4(0.43921569, 0.15686275, 0.00000000, 1.0),
	vec4(0.51764706, 0.09411765, 0.00000000, 1.0),
	vec4(0.53333333, 0.00000000, 0.00000000, 1.0),
	vec4(0.47058824, 0.00000000, 0.36078431, 1.0),
	vec4(0.28235294, 0.00000000, 0.47058824, 1.0),
	vec4(0.07843137, 0.00000000, 0.51764706, 1.0),
	vec4(0.00000000, 0.00000000, 0.53333333, 1.0),
	vec4(0.00000000, 0.09411765, 0.48627451, 1.0),
	vec4(0.00000000, 0.17254902, 0.36078431, 1.0),
	vec4(0.00000000, 0.23529412, 0.17254902, 1.0),
	vec4(0.00000000, 0.23529412, 0.00000000, 1.0),
	vec4(0.07843137, 0.21960784, 0.00000000, 1.0),
	vec4(0.17254902, 0.18823529, 0.00000000, 1.0),
	vec4(0.26666667, 0.15686275, 0.00000000, 1.0),
	vec4(0.25098039, 0.25098039, 0.25098039, 1.0),
	vec4(0.39215686, 0.39215686, 0.06274510, 1.0),
	vec4(0.51764706, 0.26666667, 0.07843137, 1.0),
	vec4(0.59607843, 0.20392157, 0.09411765, 1.0),
	vec4(0.61176471, 0.12549020, 0.12549020, 1.0),
	vec4(0.54901961, 0.12549020, 0.45490196, 1.0),
	vec4(0.37647059, 0.12549020, 0.56470588, 1.0),
	vec4(0.18823529, 0.12549020, 0.59607843, 1.0),
	vec4(0.10980392, 0.12549020, 0.61176471, 1.0),
	vec4(0.10980392, 0.21960784, 0.56470588, 1.0),
	vec4(0.10980392, 0.29803922, 0.47058824, 1.0),
	vec4(0.10980392, 0.36078431, 0.28235294, 1.0),
	vec4(0.12549020, 0.36078431, 0.12549020, 1.0),
	vec4(0.20392157, 0.36078431, 0.10980392, 1.0),
	vec4(0.29803922, 0.31372549, 0.10980392, 1.0),
	vec4(0.39215686, 0.28235294, 0.09411765, 1.0),
	vec4(0.42352941, 0.42352941, 0.42352941, 1.0),
	vec4(0.51764706, 0.51764706, 0.14117647, 1.0),
	vec4(0.59607843, 0.36078431, 0.15686275, 1.0),
	vec4(0.67450980, 0.31372549, 0.18823529, 1.0),
	vec4(0.69019608, 0.23529412, 0.23529412, 1.0),
	vec4(0.62745098, 0.23529412, 0.53333333, 1.0),
	vec4(0.47058824, 0.23529412, 0.64313725, 1.0),
	vec4(0.29803922, 0.23529412, 0.67450980, 1.0),
	vec4(0.21960784, 0.25098039, 0.69019608, 1.0),
	vec4(0.21960784, 0.32941176, 0.65882353, 1.0),
	vec4(0.21960784, 0.40784314, 0.56470588, 1.0),
	vec4(0.21960784, 0.48627451, 0.39215686, 1.0),
	vec4(0.25098039, 0.48627451, 0.25098039, 1.0),
	vec4(0.31372549, 0.48627451, 0.21960784, 1.0),
	vec4(0.40784314, 0.43921569, 0.20392157, 1.0),
	vec4(0.51764706, 0.40784314, 0.18823529, 1.0),
	vec4(0.56470588, 0.56470588, 0.56470588, 1.0),
	vec4(0.62745098, 0.62745098, 0.20392157, 1.0),
	vec4(0.67450980, 0.47058824, 0.23529412, 1.0),
	vec4(0.75294118, 0.40784314, 0.28235294, 1.0),
	vec4(0.75294118, 0.34509804, 0.34509804, 1.0),
	vec4(0.69019608, 0.34509804, 0.61176471, 1.0),
	vec4(0.54901961, 0.34509804, 0.72156863, 1.0),
	vec4(0.40784314, 0.34509804, 0.75294118, 1.0),
	vec4(0.31372549, 0.36078431, 0.75294118, 1.0),
	vec4(0.31372549, 0.43921569, 0.73725490, 1.0),
	vec4(0.31372549, 0.51764706, 0.67450980, 1.0),
	vec4(0.31372549, 0.61176471, 0.50196078, 1.0),
	vec4(0.36078431, 0.61176471, 0.36078431, 1.0),
	vec4(0.42352941, 0.59607843, 0.31372549, 1.0),
	vec4(0.51764706, 0.54901961, 0.29803922, 1.0),
	vec4(0.62745098, 0.51764706, 0.26666667, 1.0),
	vec4(0.69019608, 0.69019608, 0.69019608, 1.0),
	vec4(0.72156863, 0.72156863, 0.25098039, 1.0),
	vec4(0.73725490, 0.54901961, 0.29803922, 1.0),
	vec4(0.81568627, 0.50196078, 0.36078431, 1.0),
	vec4(0.81568627, 0.43921569, 0.43921569, 1.0),
	vec4(0.75294118, 0.43921569, 0.69019608, 1.0),
	vec4(0.62745098, 0.43921569, 0.80000000, 1.0),
	vec4(0.48627451, 0.43921569, 0.81568627, 1.0),
	vec4(0.40784314, 0.45490196, 0.81568627, 1.0),
	vec4(0.40784314, 0.53333333, 0.80000000, 1.0),
	vec4(0.40784314, 0.61176471, 0.75294118, 1.0),
	vec4(0.40784314, 0.70588235, 0.58039216, 1.0),
	vec4(0.45490196, 0.70588235, 0.45490196, 1.0),
	vec4(0.51764706, 0.70588235, 0.40784314, 1.0),
	vec4(0.61176471, 0.65882353, 0.39215686, 1.0),
	vec4(0.72156863, 0.61176471, 0.34509804, 1.0),
	vec4(0.78431373, 0.78431373, 0.78431373, 1.0),
	vec4(0.81568627, 0.81568627, 0.31372549, 1.0),
	vec4(0.80000000, 0.62745098, 0.36078431, 1.0),
	vec4(0.87843137, 0.58039216, 0.43921569, 1.0),
	vec4(0.87843137, 0.53333333, 0.53333333, 1.0),
	vec4(0.81568627, 0.51764706, 0.75294118, 1.0),
	vec4(0.70588235, 0.51764706, 0.86274510, 1.0),
	vec4(0.58039216, 0.53333333, 0.87843137, 1.0),
	vec4(0.48627451, 0.54901961, 0.87843137, 1.0),
	vec4(0.48627451, 0.61176471, 0.86274510, 1.0),
	vec4(0.48627451, 0.70588235, 0.83137255, 1.0),
	vec4(0.48627451, 0.81568627, 0.67450980, 1.0),
	vec4(0.54901961, 0.81568627, 0.54901961, 1.0),
	vec4(0.61176471, 0.80000000, 0.48627451, 1.0),
	vec4(0.70588235, 0.75294118, 0.47058824, 1.0),
	vec4(0.81568627, 0.70588235, 0.42352941, 1.0),
	vec4(0.86274510, 0.86274510, 0.86274510, 1.0),
	vec4(0.90980392, 0.90980392, 0.36078431, 1.0),
	vec4(0.86274510, 0.70588235, 0.40784314, 1.0),
	vec4(0.92549020, 0.65882353, 0.50196078, 1.0),
	vec4(0.92549020, 0.62745098, 0.62745098, 1.0),
	vec4(0.86274510, 0.61176471, 0.81568627, 1.0),
	vec4(0.76862745, 0.61176471, 0.92549020, 1.0),
	vec4(0.65882353, 0.62745098, 0.92549020, 1.0),
	vec4(0.56470588, 0.64313725, 0.92549020, 1.0),
	vec4(0.56470588, 0.70588235, 0.92549020, 1.0),
	vec4(0.56470588, 0.80000000, 0.90980392, 1.0),
	vec4(0.56470588, 0.89411765, 0.75294118, 1.0),
	vec4(0.64313725, 0.89411765, 0.64313725, 1.0),
	vec4(0.70588235, 0.89411765, 0.56470588, 1.0),
	vec4(0.80000000, 0.83137255, 0.53333333, 1.0),
	vec4(0.90980392, 0.80000000, 0.48627451, 1.0),
	vec4(0.92549020, 0.92549020, 0.92549020, 1.0),
	vec4(0.98823529, 0.98823529, 0.40784314, 1.0),
	vec4(0.92549020, 0.78431373, 0.47058824, 1.0),
	vec4(0.98823529, 0.73725490, 0.58039216, 1.0),
	vec4(0.98823529, 0.70588235, 0.70588235, 1.0),
	vec4(0.92549020, 0.69019608, 0.87843137, 1.0),
	vec4(0.83137255, 0.69019608, 0.98823529, 1.0),
	vec4(0.73725490, 0.70588235, 0.98823529, 1.0),
	vec4(0.64313725, 0.72156863, 0.98823529, 1.0),
	vec4(0.64313725, 0.78431373, 0.98823529, 1.0),
	vec4(0.64313725, 0.87843137, 0.98823529, 1.0),
	vec4(0.64313725, 0.98823529, 0.83137255, 1.0),
	vec4(0.72156863, 0.98823529, 0.72156863, 1.0),
	vec4(0.78431373, 0.98823529, 0.64313725, 1.0),
	vec4(0.87843137, 0.92549020, 0.61176471, 1.0),
	vec4(0.98823529, 0.87843137, 0.54901961, 1.0)
);

struct UboData
{
	mat4 projection;
	mat4 modelview;
	mat4 inverseProjModelView;
	vec4 lightPos;
	uint highlightedShaderPermutation;	// for debugging classified modes
};

vec4 getPaletteColor(uint wrappableIndex)
{
	return COLOR_PALETTE[wrappableIndex % COLOR_PALETTE.length()];
}

vec4 getMaterialColor(uint wrappableIndex)
{
	return MATERIAL_MAP_COLORS[wrappableIndex % MATERIAL_MAP_COLORS.length()];
}

vec3 toSrgb(vec3 linear)
{
	// From Khronos Data Format spec, 13.3.2 sRGB EOTF-1
	const float InvGamma = 0.4166666666666667;  // 1.0 / 2.4
	vec3  low    = 12.92 * linear;
	vec3  high   = 1.055 * pow(linear, vec3(InvGamma)) - 0.055;
	bvec3 select = bvec3(step(0.0031308, linear));
	return mix(low, high, select);
}

// Simplex 3D noise implementation from: https://www.shadertoy.com/view/XsX3zB
// Copyright (c) 2013 Nikita Miropolskiy (MIT License)
//
// functions: random3, simplex3d

/* discontinuous pseudorandom uniformly distributed in [-0.5, +0.5]^3 */
vec3 random3(vec3 c) {
	float j = 4096.0*sin(dot(c,vec3(17.0, 59.4, 15.0)));
	vec3 r;
	r.z = fract(512.0*j);
	j *= .125;
	r.x = fract(512.0*j);
	j *= .125;
	r.y = fract(512.0*j);
	return r-0.5;
}

/* 3d simplex noise */
float simplex3d(vec3 p) {
	/* skew constants for 3d simplex functions */
	const float F3 =  0.3333333;
	const float G3 =  0.1666667;

	 /* 1. find current tetrahedron T and it's four vertices */
	 /* s, s+i1, s+i2, s+1.0 - absolute skewed (integer) coordinates of T vertices */
	 /* x, x1, x2, x3 - unskewed coordinates of p relative to each of T vertices*/

	 /* calculate s and x */
	 vec3 s = floor(p + dot(p, vec3(F3)));
	 vec3 x = p - s + dot(s, vec3(G3));

	 /* calculate i1 and i2 */
	 vec3 e = step(vec3(0.0), x - x.yzx);
	 vec3 i1 = e*(1.0 - e.zxy);
	 vec3 i2 = 1.0 - e.zxy*(1.0 - e);

	 /* x1, x2, x3 */
	 vec3 x1 = x - i1 + G3;
	 vec3 x2 = x - i2 + 2.0*G3;
	 vec3 x3 = x - 1.0 + 3.0*G3;

	 /* 2. find four surflets and store them in d */
	 vec4 w, d;

	 /* calculate surflet weights */
	 w.x = dot(x, x);
	 w.y = dot(x1, x1);
	 w.z = dot(x2, x2);
	 w.w = dot(x3, x3);

	 /* w fades from 0.6 at the center of the surflet to 0.0 at the margin */
	 w = max(0.6 - w, 0.0);

	 /* calculate surflet components */
	 d.x = dot(random3(s), x);
	 d.y = dot(random3(s + i1), x1);
	 d.z = dot(random3(s + i2), x2);
	 d.w = dot(random3(s + 1.0), x3);

	 /* multiply d by w^4 */
	 w *= w;
	 w *= w;
	 d *= w;

	 /* 3. return the sum of the four surflets */
	 return dot(d, vec4(52.0));
}

// Gradient 2D noise implementation from: https://www.shadertoy.com/view/XdXBRH
// Copyright (c) 2017 Inigo Quilez (MIT License)
//
// functions: hash, noised

vec2 hash( in vec2 x )  // replace this by something better
{
    const vec2 k = vec2( 0.3183099, 0.3678794 );
    x = x*k + k.yx;
    return -1.0 + 2.0*fract( 16.0 * k*fract( x.x*x.y*(x.x+x.y)) );
}

// return gradient noise (in x) and its derivatives (in yz)
vec3 noised( in vec2 p )
{
    vec2 i = floor( p );
    vec2 f = fract( p );

    // quintic interpolation
    vec2 u = f*f*f*(f*(f*6.0-15.0)+10.0);
    vec2 du = 30.0*f*f*(f*(f-2.0)+1.0);

    vec2 ga = hash( i + vec2(0.0,0.0) );
    vec2 gb = hash( i + vec2(1.0,0.0) );
    vec2 gc = hash( i + vec2(0.0,1.0) );
    vec2 gd = hash( i + vec2(1.0,1.0) );

    float va = dot( ga, f - vec2(0.0,0.0) );
    float vb = dot( gb, f - vec2(1.0,0.0) );
    float vc = dot( gc, f - vec2(0.0,1.0) );
    float vd = dot( gd, f - vec2(1.0,1.0) );

    return vec3( va + u.x*(vb-va) + u.y*(vc-va) + u.x*u.y*(va-vb-vc+vd),   // value
                 ga + u.x*(gb-ga) + u.y*(gc-ga) + u.x*u.y*(ga-gb-gc+gd) +  // derivatives
                 du * (u.yx*(va-vb-vc+vd) + vec2(vb,vc) - va));
}

float iterateNoise(vec3 base, float aluComplexity)
{
	float noise = 0.0;
	float numIter = clamp(aluComplexity * 128.0, 1.0, 128.0);
	for (float i = 1.0; i <= numIter; i += 1.0)
	{
		mat3 M = mat3(
			i, 0, 0,
			0, tan(i), -4.0*sin(i),
			1, 2.0*sin(i), tan(i)
		);
		noise += ((1.0 / i) * simplex3d(M*base*i));
	}
	return noise;
}

vec3 calculateModelColor(
	UboData ubo,
	vec3 	albedo,
	vec3 	normal,
	ivec2 	coord,
	float 	depth,
	ivec2 	viewSize,
	float 	aluComplexity)
{
	vec2 uv  = vec2(coord) / vec2(viewSize);
	vec4 ndc = vec4(2.0 * uv - 1.0, depth, 1.0);
	vec4 pos = ubo.inverseProjModelView * ndc;
	pos /= pos.w;

	float noise = iterateNoise(normal, aluComplexity);
	vec3 tint = vec3(1.0, 0.7, 0.3) * vec3(0.5 + (0.4 * noise));

	vec3 viewPos = (ubo.modelview * pos).xyz;
	vec3 L = normalize(mat3(ubo.modelview) * ubo.lightPos.xyz - viewPos);
	vec3 V = normalize(-viewPos);
	vec3 R = reflect(-L, normal);
	vec3 diffuse = max(dot(normal, L), 0.0) * albedo;
	vec3 specular = pow(max(dot(R, V), 0.0), 32.0) * vec3(1.0);

	return diffuse * tint + specular;
}

vec3 calculateBackgroundColor(UboData ubo, ivec2 coord, ivec2 viewSize, float aluComplexity)
{
	vec2 uv = vec2(coord) / vec2(viewSize);
	vec2 p  = 2.0 * uv - 1.0;
	p = (ubo.projection * vec4(2.0*p, 0.0, 1.0)).xy;
	p.y -= 2.2;
	float r = length(p);

	const float PI = 3.141592653589793;
	const float t  = 0.4;
	vec3 color = vec3(1.0);
	color = (r < PI) ? vec3(cos(t*r)) : vec3(cos(t*PI));

	const float numIter = clamp(aluComplexity * 64.0, 1.0, 64.0);
	for (float i = 0; i < numIter; ++i)
	{
		color += (0.25/numIter) * vec3(
			dot(noised(120.0 * (p + hash(vec2(i, i)))),
				vec3(0, 1, 0)));
	}

	return color;
}
