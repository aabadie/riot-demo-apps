
#include <eml_net.h>
static const float pong_model_layer_0_biases[16] = { 0.976157f, 0.285028f, -0.443334f, 1.856706f, -2.844906f, 0.650198f, 1.460820f, 0.215186f, 1.974909f, -0.649530f, -0.481839f, -0.415633f, 1.185954f, 0.431517f, 0.118470f, 2.002454f };
static const float pong_model_layer_0_weights[112] = { -0.432984f, 1.262619f, -0.000000f, -1.615529f, -1.237664f, -1.622630f, -0.472363f, 0.945884f, -1.679729f, -0.533429f, 0.000000f, -0.000000f, -1.394937f, -0.656843f, -2.522042f, -0.295547f, 0.366046f, 0.153491f, 0.000000f, -1.249190f, 1.946215f, 1.865341f, -0.492796f, -0.077629f, -0.115759f, 1.062430f, 0.000000f, 0.000000f, -0.540416f, 0.838821f, -0.148666f, -1.728650f, 0.606401f, 0.548636f, -0.000000f, -2.890034f, -6.039057f, 1.145339f, -0.614619f, 0.153240f, 0.843919f, 0.479613f, -0.000000f, 0.000000f, -0.674513f, 0.463308f, 0.597740f, -4.011881f, -1.866669f, 3.468865f, -0.000000f, -2.445669f, 0.005767f, -0.981146f, 2.890620f, -0.690110f, -0.008725f, 1.830693f, -0.000000f, 0.000000f, -4.780756f, -0.409478f, -0.365446f, 0.046242f, -0.514976f, 0.903628f, -0.000000f, -0.427752f, 4.848839f, 1.648242f, -0.840160f, -1.388422f, -3.012422f, 1.553523f, -0.000000f, 0.000000f, 0.950626f, 0.264200f, 1.169410f, -4.257085f, 1.982188f, -2.123942f, 0.000000f, -2.482860f, -4.413179f, 0.909630f, 1.656086f, 2.052652f, 2.722089f, -1.076830f, -0.000000f, -0.000000f, -2.704493f, 0.445966f, -1.272043f, 4.283607f, 0.999067f, 0.837305f, -0.000000f, -0.086803f, -0.817275f, 0.474771f, 0.393080f, 0.363798f, -0.441239f, -0.558276f, 0.000000f, -0.000000f, 0.518077f, 1.194028f, -0.024497f, -0.651213f };
static const float pong_model_layer_1_biases[3] = { 0.136744f, -0.045258f, -0.702868f };
static const float pong_model_layer_1_weights[48] = { -1.463521f, 0.837886f, -0.273531f, 1.879948f, 0.263951f, -2.137897f, 0.000000f, -0.000000f, 0.000000f, -4.702016f, 0.744028f, 4.323564f, 7.766027f, -0.514423f, -9.678349f, -1.671799f, -0.922961f, 1.181439f, -3.017590f, 0.201513f, 2.664628f, -2.236736f, -0.801931f, 2.787055f, 3.670213f, -1.708415f, -4.597163f, -2.864094f, -1.164002f, 2.722286f, 0.000000f, 0.000000f, -0.000000f, -0.000000f, 0.000000f, 0.000000f, 2.616682f, -1.249625f, -3.724040f, -0.433364f, 0.745938f, -0.274581f, -1.427672f, -1.665931f, 3.453820f, -10.409451f, -1.078159f, 10.315833f };
static float pong_model_buf1[16];
static float pong_model_buf2[16];
static const EmlNetLayer pong_model_layers[2] = { 
{ 16, 7, pong_model_layer_0_weights, pong_model_layer_0_biases, EmlNetActivationRelu }, 
{ 3, 16, pong_model_layer_1_weights, pong_model_layer_1_biases, EmlNetActivationSoftmax } };
static EmlNet pong_model = { 2, pong_model_layers, pong_model_buf1, pong_model_buf2, 16 };

    int32_t
    pong_model_predict(const float *features, int32_t n_features)
    {
        return eml_net_predict(&pong_model, features, n_features);
    }
    

    int32_t
    pong_model_regress(const float *features, int32_t n_features, float *out, int32_t out_length)
    {
        return eml_net_regress(&pong_model, features, n_features, out, out_length);
    }
    

    float
    pong_model_regress1(const float *features, int32_t n_features)
    {
        return eml_net_regress1(&pong_model, features, n_features);
    }
    