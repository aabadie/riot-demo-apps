


    // !!! This file is generated using emlearn !!!

    #include <eml_trees.h>
    

static const EmlTreesNode sound_model_nodes[97] = {
  { 36, 3480, 1, -1 },
  { 37, 49, -2, 1 },
  { 1, -364, -3, 1 },
  { 25, -129, -4, -3 },
  { 36, 907, -4, 1 },
  { 38, 89, -2, 1 },
  { 37, 279, 1, 2 },
  { 24, -188, -1, -3 },
  { 22, -256, -1, -3 },
  { 19, -240, 1, -4 },
  { 30, -151, 1, 2 },
  { 38, 18, -2, -4 },
  { 14, -177, 1, 2 },
  { 36, 3580, -3, -1 },
  { 7, -248, -1, -3 },
  { 31, -132, 1, -1 },
  { 16, -240, 1, -4 },
  { 22, -264, -2, -3 },
  { 18, -207, 1, -4 },
  { 30, -140, -2, 1 },
  { 14, -236, -3, 1 },
  { 32, 4, 1, 2 },
  { 15, -253, -3, -1 },
  { 9, -345, -1, -3 },
  { 30, -150, 1, 2 },
  { 18, -256, -2, -4 },
  { 13, -178, 1, 2 },
  { 15, -281, -3, -1 },
  { 7, -264, -1, -3 },
  { 16, -213, 1, -4 },
  { 36, 3536, 1, -1 },
  { 33, 2, -3, -2 },
  { 28, -135, 1, 2 },
  { 18, -251, -2, -4 },
  { 37, 278, 1, 3 },
  { 36, 3536, 1, -1 },
  { 38, 112, -2, -3 },
  { 34, 97, -3, -1 },
  { 38, 10, 1, 2 },
  { 21, -296, -2, -4 },
  { 10, -332, 1, 2 },
  { 16, -306, -3, -4 },
  { 32, 5, 1, 5 },
  { 20, -271, 1, -3 },
  { 0, -293, 1, 2 },
  { 7, -265, -1, -3 },
  { 22, -273, -1, -3 },
  { 39, 385, -4, -3 },
  { 16, -240, 1, 3 },
  { 32, 18, 1, -2 },
  { 15, -245, -3, -1 },
  { 18, -250, -1, -4 },
  { 29, -138, 1, 3 },
  { 16, -316, -2, 1 },
  { 9, -310, -4, -1 },
  { 38, 274, 1, 2 },
  { 24, -261, -1, -3 },
  { 27, -125, 1, 2 },
  { 2, -437, -3, -1 },
  { 15, -277, -3, -1 },
  { 29, -161, 1, 2 },
  { 18, -246, -2, -4 },
  { 31, -129, -3, -1 },
  { 19, -227, 1, -4 },
  { 31, -129, 1, -1 },
  { 33, 2, -3, -2 },
  { 29, -138, 1, 3 },
  { 2, -416, 1, -4 },
  { 20, -311, -2, -1 },
  { 13, -191, 1, 2 },
  { 22, -256, -1, -3 },
  { 31, -132, -3, -1 },
  { 17, -205, 1, -4 },
  { 36, 3536, 1, -1 },
  { 39, 338, -2, -3 },
  { 36, 3633, 1, -1 },
  { 16, -250, 1, -4 },
  { 7, -369, 1, -3 },
  { 0, -508, -3, -2 },
  { 16, -240, 1, -4 },
  { 38, 88, -2, 1 },
  { 36, 3480, -3, -1 },
  { 15, -257, 1, -1 },
  { 29, -139, 1, -3 },
  { 37, 58, -2, -4 },
  { 32, 5, 1, 4 },
  { 27, -142, 1, 2 },
  { 32, 4, -4, -3 },
  { 31, -133, -3, -1 },
  { 28, -142, 1, 4 },
  { 3, -421, 1, 2 },
  { 17, -188, -2, -4 },
  { 18, -252, -2, -4 },
  { 15, -316, -3, -2 },
  { 17, -192, 1, -4 },
  { 33, 2, 1, -2 },
  { 23, -234, -1, -3 } 
};

static const int32_t sound_model_tree_roots[20] = { 0, 4, 9, 15, 18, 24, 29, 32, 38, 48, 52, 60, 63, 66, 72, 75, 79, 82, 85, 94 };

static const uint8_t sound_model_leaves[4] = { 1, 2, 0, 3 };

EmlTrees sound_model = {
        97,
        (EmlTreesNode *)(sound_model_nodes),	  
        20,
        (int32_t *)(sound_model_tree_roots),
        4,
        (uint8_t *)(sound_model_leaves),
        0,
        40,
        4,
    };

static inline int32_t sound_model_tree_0(const int16_t *features, int32_t features_length) {
          if (features[36] < 3480) {
              if (features[37] < 49) {
                  return 2;
              } else {
                  if (features[1] < -364) {
                      return 0;
                  } else {
                      if (features[25] < -129) {
                          return 3;
                      } else {
                          return 0;
                      }
                  }
              }
          } else {
              return 1;
          }
        }
        

static inline int32_t sound_model_tree_1(const int16_t *features, int32_t features_length) {
          if (features[36] < 907) {
              return 3;
          } else {
              if (features[38] < 89) {
                  return 2;
              } else {
                  if (features[37] < 279) {
                      if (features[24] < -188) {
                          return 1;
                      } else {
                          return 0;
                      }
                  } else {
                      if (features[22] < -256) {
                          return 1;
                      } else {
                          return 0;
                      }
                  }
              }
          }
        }
        

static inline int32_t sound_model_tree_2(const int16_t *features, int32_t features_length) {
          if (features[19] < -240) {
              if (features[30] < -151) {
                  if (features[38] < 18) {
                      return 2;
                  } else {
                      return 3;
                  }
              } else {
                  if (features[14] < -177) {
                      if (features[36] < 3580) {
                          return 0;
                      } else {
                          return 1;
                      }
                  } else {
                      if (features[7] < -248) {
                          return 1;
                      } else {
                          return 0;
                      }
                  }
              }
          } else {
              return 3;
          }
        }
        

static inline int32_t sound_model_tree_3(const int16_t *features, int32_t features_length) {
          if (features[31] < -132) {
              if (features[16] < -240) {
                  if (features[22] < -264) {
                      return 2;
                  } else {
                      return 0;
                  }
              } else {
                  return 3;
              }
          } else {
              return 1;
          }
        }
        

static inline int32_t sound_model_tree_4(const int16_t *features, int32_t features_length) {
          if (features[18] < -207) {
              if (features[30] < -140) {
                  return 2;
              } else {
                  if (features[14] < -236) {
                      return 0;
                  } else {
                      if (features[32] < 4) {
                          if (features[15] < -253) {
                              return 0;
                          } else {
                              return 1;
                          }
                      } else {
                          if (features[9] < -345) {
                              return 1;
                          } else {
                              return 0;
                          }
                      }
                  }
              }
          } else {
              return 3;
          }
        }
        

static inline int32_t sound_model_tree_5(const int16_t *features, int32_t features_length) {
          if (features[30] < -150) {
              if (features[18] < -256) {
                  return 2;
              } else {
                  return 3;
              }
          } else {
              if (features[13] < -178) {
                  if (features[15] < -281) {
                      return 0;
                  } else {
                      return 1;
                  }
              } else {
                  if (features[7] < -264) {
                      return 1;
                  } else {
                      return 0;
                  }
              }
          }
        }
        

static inline int32_t sound_model_tree_6(const int16_t *features, int32_t features_length) {
          if (features[16] < -213) {
              if (features[36] < 3536) {
                  if (features[33] < 2) {
                      return 0;
                  } else {
                      return 2;
                  }
              } else {
                  return 1;
              }
          } else {
              return 3;
          }
        }
        

static inline int32_t sound_model_tree_7(const int16_t *features, int32_t features_length) {
          if (features[28] < -135) {
              if (features[18] < -251) {
                  return 2;
              } else {
                  return 3;
              }
          } else {
              if (features[37] < 278) {
                  if (features[36] < 3536) {
                      if (features[38] < 112) {
                          return 2;
                      } else {
                          return 0;
                      }
                  } else {
                      return 1;
                  }
              } else {
                  if (features[34] < 97) {
                      return 0;
                  } else {
                      return 1;
                  }
              }
          }
        }
        

static inline int32_t sound_model_tree_8(const int16_t *features, int32_t features_length) {
          if (features[38] < 10) {
              if (features[21] < -296) {
                  return 2;
              } else {
                  return 3;
              }
          } else {
              if (features[10] < -332) {
                  if (features[16] < -306) {
                      return 0;
                  } else {
                      return 3;
                  }
              } else {
                  if (features[32] < 5) {
                      if (features[20] < -271) {
                          if (features[0] < -293) {
                              if (features[7] < -265) {
                                  return 1;
                              } else {
                                  return 0;
                              }
                          } else {
                              if (features[22] < -273) {
                                  return 1;
                              } else {
                                  return 0;
                              }
                          }
                      } else {
                          return 0;
                      }
                  } else {
                      if (features[39] < 385) {
                          return 3;
                      } else {
                          return 0;
                      }
                  }
              }
          }
        }
        

static inline int32_t sound_model_tree_9(const int16_t *features, int32_t features_length) {
          if (features[16] < -240) {
              if (features[32] < 18) {
                  if (features[15] < -245) {
                      return 0;
                  } else {
                      return 1;
                  }
              } else {
                  return 2;
              }
          } else {
              if (features[18] < -250) {
                  return 1;
              } else {
                  return 3;
              }
          }
        }
        

static inline int32_t sound_model_tree_10(const int16_t *features, int32_t features_length) {
          if (features[29] < -138) {
              if (features[16] < -316) {
                  return 2;
              } else {
                  if (features[9] < -310) {
                      return 3;
                  } else {
                      return 1;
                  }
              }
          } else {
              if (features[38] < 274) {
                  if (features[24] < -261) {
                      return 1;
                  } else {
                      return 0;
                  }
              } else {
                  if (features[27] < -125) {
                      if (features[2] < -437) {
                          return 0;
                      } else {
                          return 1;
                      }
                  } else {
                      if (features[15] < -277) {
                          return 0;
                      } else {
                          return 1;
                      }
                  }
              }
          }
        }
        

static inline int32_t sound_model_tree_11(const int16_t *features, int32_t features_length) {
          if (features[29] < -161) {
              if (features[18] < -246) {
                  return 2;
              } else {
                  return 3;
              }
          } else {
              if (features[31] < -129) {
                  return 0;
              } else {
                  return 1;
              }
          }
        }
        

static inline int32_t sound_model_tree_12(const int16_t *features, int32_t features_length) {
          if (features[19] < -227) {
              if (features[31] < -129) {
                  if (features[33] < 2) {
                      return 0;
                  } else {
                      return 2;
                  }
              } else {
                  return 1;
              }
          } else {
              return 3;
          }
        }
        

static inline int32_t sound_model_tree_13(const int16_t *features, int32_t features_length) {
          if (features[29] < -138) {
              if (features[2] < -416) {
                  if (features[20] < -311) {
                      return 2;
                  } else {
                      return 1;
                  }
              } else {
                  return 3;
              }
          } else {
              if (features[13] < -191) {
                  if (features[22] < -256) {
                      return 1;
                  } else {
                      return 0;
                  }
              } else {
                  if (features[31] < -132) {
                      return 0;
                  } else {
                      return 1;
                  }
              }
          }
        }
        

static inline int32_t sound_model_tree_14(const int16_t *features, int32_t features_length) {
          if (features[17] < -205) {
              if (features[36] < 3536) {
                  if (features[39] < 338) {
                      return 2;
                  } else {
                      return 0;
                  }
              } else {
                  return 1;
              }
          } else {
              return 3;
          }
        }
        

static inline int32_t sound_model_tree_15(const int16_t *features, int32_t features_length) {
          if (features[36] < 3633) {
              if (features[16] < -250) {
                  if (features[7] < -369) {
                      if (features[0] < -508) {
                          return 0;
                      } else {
                          return 2;
                      }
                  } else {
                      return 0;
                  }
              } else {
                  return 3;
              }
          } else {
              return 1;
          }
        }
        

static inline int32_t sound_model_tree_16(const int16_t *features, int32_t features_length) {
          if (features[16] < -240) {
              if (features[38] < 88) {
                  return 2;
              } else {
                  if (features[36] < 3480) {
                      return 0;
                  } else {
                      return 1;
                  }
              }
          } else {
              return 3;
          }
        }
        

static inline int32_t sound_model_tree_17(const int16_t *features, int32_t features_length) {
          if (features[15] < -257) {
              if (features[29] < -139) {
                  if (features[37] < 58) {
                      return 2;
                  } else {
                      return 3;
                  }
              } else {
                  return 0;
              }
          } else {
              return 1;
          }
        }
        

static inline int32_t sound_model_tree_18(const int16_t *features, int32_t features_length) {
          if (features[32] < 5) {
              if (features[27] < -142) {
                  if (features[32] < 4) {
                      return 3;
                  } else {
                      return 0;
                  }
              } else {
                  if (features[31] < -133) {
                      return 0;
                  } else {
                      return 1;
                  }
              }
          } else {
              if (features[28] < -142) {
                  if (features[3] < -421) {
                      if (features[17] < -188) {
                          return 2;
                      } else {
                          return 3;
                      }
                  } else {
                      if (features[18] < -252) {
                          return 2;
                      } else {
                          return 3;
                      }
                  }
              } else {
                  if (features[15] < -316) {
                      return 0;
                  } else {
                      return 2;
                  }
              }
          }
        }
        

static inline int32_t sound_model_tree_19(const int16_t *features, int32_t features_length) {
          if (features[17] < -192) {
              if (features[33] < 2) {
                  if (features[23] < -234) {
                      return 1;
                  } else {
                      return 0;
                  }
              } else {
                  return 2;
              }
          } else {
              return 3;
          }
        }
        

int32_t sound_model_predict(const int16_t *features, int32_t features_length) {

        int32_t votes[4] = {0,};
        int32_t _class = -1;

        _class = sound_model_tree_0(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_1(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_2(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_3(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_4(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_5(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_6(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_7(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_8(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_9(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_10(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_11(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_12(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_13(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_14(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_15(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_16(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_17(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_18(features, features_length); votes[_class] += 1;
    _class = sound_model_tree_19(features, features_length); votes[_class] += 1;
    
        int32_t most_voted_class = -1;
        int32_t most_voted_votes = 0;
        for (int32_t i=0; i<4; i++) {

            if (votes[i] > most_voted_votes) {
                most_voted_class = i;
                most_voted_votes = votes[i];
            }
        }
        return most_voted_class;
    }
    