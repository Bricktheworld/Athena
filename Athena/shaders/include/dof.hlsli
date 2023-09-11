#pragma once

static const int kKernelRadius = 8;
//static const int kKernelCount = 17;

static const int kDilateSize = 8;

static const half kMaxCoC = 2.0h;

static const float4 kKernel0BracketsRealXY_ImZW = float4(-0.045884,1.097245,-0.033796,0.838935);
static const float2 kKernel0Weights_RealX_ImY = float2(0.411259,-0.548794);
static const float4 kKernel0_RealX_ImY_RealZ_ImW[] =
{
	float4(/*XY: Non Bracketed*/0.005645,0.020657,/*Bracketed WZ:*/0.046962,0.064907),
	float4(/*XY: Non Bracketed*/0.025697,-0.013190,/*Bracketed WZ:*/0.065237,0.024562),
	float4(/*XY: Non Bracketed*/-0.016100,-0.033796,/*Bracketed WZ:*/0.027145,0.000000),
	float4(/*XY: Non Bracketed*/-0.045884,0.008247,/*Bracketed WZ:*/0.000000,0.050114),
	float4(/*XY: Non Bracketed*/-0.017867,0.052848,/*Bracketed WZ:*/0.025534,0.103278),
	float4(/*XY: Non Bracketed*/0.030969,0.056175,/*Bracketed WZ:*/0.070042,0.107244),
	float4(/*XY: Non Bracketed*/0.063053,0.032363,/*Bracketed WZ:*/0.099282,0.078860),
	float4(/*XY: Non Bracketed*/0.074716,0.008899,/*Bracketed WZ:*/0.109911,0.050892),
	float4(/*XY: Non Bracketed*/0.076760,0.000000,/*Bracketed WZ:*/0.111774,0.040284),
	float4(/*XY: Non Bracketed*/0.074716,0.008899,/*Bracketed WZ:*/0.109911,0.050892),
	float4(/*XY: Non Bracketed*/0.063053,0.032363,/*Bracketed WZ:*/0.099282,0.078860),
	float4(/*XY: Non Bracketed*/0.030969,0.056175,/*Bracketed WZ:*/0.070042,0.107244),
	float4(/*XY: Non Bracketed*/-0.017867,0.052848,/*Bracketed WZ:*/0.025534,0.103278),
	float4(/*XY: Non Bracketed*/-0.045884,0.008247,/*Bracketed WZ:*/0.000000,0.050114),
	float4(/*XY: Non Bracketed*/-0.016100,-0.033796,/*Bracketed WZ:*/0.027145,0.000000),
	float4(/*XY: Non Bracketed*/0.025697,-0.013190,/*Bracketed WZ:*/0.065237,0.024562),
	float4(/*XY: Non Bracketed*/0.005645,0.020657,/*Bracketed WZ:*/0.046962,0.064907)
};
static const float4 kKernel1BracketsRealXY_ImZW = float4(-0.002843,0.595479,0.000000,0.189160);
static const float2 kKernel1Weights_RealX_ImY   = float2(0.513282,4.561110);
static const float4 kKernel1_RealX_ImY_RealZ_ImW[] =
{
	float4(/*XY: Non Bracketed*/-0.002843,0.003566,/*Bracketed WZ:*/0.000000,0.018854),
	float4(/*XY: Non Bracketed*/-0.001296,0.008744,/*Bracketed WZ:*/0.002598,0.046224),
	float4(/*XY: Non Bracketed*/0.004764,0.014943,/*Bracketed WZ:*/0.012775,0.078998),
	float4(/*XY: Non Bracketed*/0.016303,0.019581,/*Bracketed WZ:*/0.032153,0.103517),
	float4(/*XY: Non Bracketed*/0.032090,0.020162,/*Bracketed WZ:*/0.058664,0.106584),
	float4(/*XY: Non Bracketed*/0.049060,0.016015,/*Bracketed WZ:*/0.087162,0.084666),
	float4(/*XY: Non Bracketed*/0.063712,0.008994,/*Bracketed WZ:*/0.111767,0.047547),
	float4(/*XY: Non Bracketed*/0.073402,0.002575,/*Bracketed WZ:*/0.128041,0.013610),
	float4(/*XY: Non Bracketed*/0.076760,0.000000,/*Bracketed WZ:*/0.133679,0.000000),
	float4(/*XY: Non Bracketed*/0.073402,0.002575,/*Bracketed WZ:*/0.128041,0.013610),
	float4(/*XY: Non Bracketed*/0.063712,0.008994,/*Bracketed WZ:*/0.111767,0.047547),
	float4(/*XY: Non Bracketed*/0.049060,0.016015,/*Bracketed WZ:*/0.087162,0.084666),
	float4(/*XY: Non Bracketed*/0.032090,0.020162,/*Bracketed WZ:*/0.058664,0.106584),
	float4(/*XY: Non Bracketed*/0.016303,0.019581,/*Bracketed WZ:*/0.032153,0.103517),
	float4(/*XY: Non Bracketed*/0.004764,0.014943,/*Bracketed WZ:*/0.012775,0.078998),
	float4(/*XY: Non Bracketed*/-0.001296,0.008744,/*Bracketed WZ:*/0.002598,0.046224),
	float4(/*XY: Non Bracketed*/-0.002843,0.003566,/*Bracketed WZ:*/0.000000,0.018854)
};

float4 compute_c0_xy_c1_zw(float input, int kernel_index)
{
	float2 c0 = kKernel0_RealX_ImY_RealZ_ImW[kernel_index + kKernelRadius].xy;
	float2 c1 = kKernel1_RealX_ImY_RealZ_ImW[kernel_index + kKernelRadius].xy;

	float2 input2 = float2(input, input);

	float4 ret;
	ret.xy = input2 * c0;
	ret.zw = input2 * c1;

	return ret;
}

float2 mult_complex(float2 p, float2 q)
{
	// (Pr+Pi)*(Qr+Qi) = (Pr*Qr+Pr*Qi+Pi*Qr-Pi*Qi)
	return float2(p.x * q.x - p.y * q.y, p.x * q.y + p.y * q.x);
}