
#include <eml_net.h>
static const float pong_model_layer_0_biases[16] = { 1.727860f, 0.712096f, -0.443334f, 2.290941f, -3.324447f, 0.054392f, 1.374648f, 1.267933f, 1.868136f, -1.769396f, -0.481839f, -0.418141f, 0.286309f, 0.119944f, 0.098598f, 1.289337f };
static const float pong_model_layer_0_weights[112] = { 0.237920f, 0.924514f, 0.000000f, -0.949439f, -1.040610f, 0.492490f, -0.539621f, -0.239002f, -0.642593f, 0.740302f, -0.000000f, -0.000000f, -0.857808f, 0.029929f, -2.157557f, -0.220490f, -0.464979f, 0.664464f, 0.000000f, -1.682304f, 2.139591f, 1.298348f, -1.349124f, -0.903481f, -0.338984f, 0.553558f, 0.000000f, 0.000000f, -0.768127f, -0.162317f, 0.115023f, -1.011835f, 0.746780f, 0.420649f, -0.000000f, -4.307328f, -6.117008f, 0.920587f, 0.003877f, -2.727313f, 1.432252f, 1.523034f, -0.000000f, 0.000000f, 0.266502f, 2.043829f, 0.876630f, -2.032444f, -2.761600f, 2.833505f, -0.000000f, 0.420423f, 0.558801f, 5.135817f, -0.080785f, -0.132558f, 0.568682f, -0.173959f, 0.000000f, -0.000000f, -4.098095f, 0.802759f, 0.276792f, 0.173649f, -0.769706f, 0.795980f, -0.000000f, -3.963319f, 5.062890f, 0.002097f, -1.359054f, -3.089606f, -3.218519f, 0.410882f, 0.000000f, 0.000000f, 1.389450f, -1.050804f, 1.628298f, -2.273389f, -0.445867f, -0.122764f, 0.000000f, 4.207804f, -4.459707f, 4.139347f, 0.186832f, 2.607320f, 2.501734f, 1.466667f, 0.000000f, 0.000000f, -4.055227f, 0.860501f, -1.625890f, 2.479263f, 1.504173f, 1.163805f, -0.000000f, -0.540667f, -0.834055f, -0.064433f, 1.276946f, -0.455381f, 0.291776f, -0.123744f, 0.000000f, -0.000000f, 0.356588f, 0.175822f, -0.583481f, -0.758521f };
static const float pong_model_layer_1_biases[3] = { -0.576856f, -0.170111f, 0.189964f };
static const float pong_model_layer_1_weights[48] = { -2.347001f, 0.697312f, 1.044723f, 1.888096f, 0.118469f, -1.851852f, 0.000000f, 0.000000f, -0.000000f, -8.858187f, 1.790169f, 6.531872f, 9.214981f, -0.559750f, -11.136493f, -3.196950f, -0.644199f, 1.766014f, -2.474962f, 0.657925f, 2.716646f, -6.301970f, -1.552426f, 7.660140f, 3.691658f, -2.358675f, -3.526277f, -6.132226f, -1.453200f, 4.244206f, -0.000000f, -0.000000f, 0.000000f, -0.000000f, 0.000000f, 0.000000f, 2.628990f, -1.393436f, -3.502731f, 0.184171f, 1.272711f, -1.680635f, -2.644472f, -1.556601f, 4.260813f, -4.098773f, -2.665057f, 7.061639f };
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
    