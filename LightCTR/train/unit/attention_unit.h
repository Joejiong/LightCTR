//
//  attention_unit.h
//  LightCTR
//
//  Created by SongKuangshi on 2017/11/2.
//  Copyright © 2017年 SongKuangshi. All rights reserved.
//

#ifndef attention_unit_h
#define attention_unit_h

#include <vector>
#include "../../util/matrix.h"
#include "../layer/fullyconnLayer.h"

// Attention-based Encoder-Decoder build a RNN that has alignment attention
template <typename ActivationFunction>
class Attention_Unit : public Layer_Base {
public:
    Attention_Unit(size_t _dimension, size_t _hidden_size, size_t _recurrent_cnt):
    Layer_Base(NULL, _recurrent_cnt, _dimension), dimension(_dimension), batch_size(_recurrent_cnt) {
        this->activeFun = new ActivationFunction();
        
        printf("Attention-based Unit\n");
        // alpha transform is computed by DxH and Hx1 fc Layer
        printf("-- Attention Inner FC-1 ");
        transformFunc = new Fully_Conn_Layer<Sigmoid>(NULL, _dimension, _hidden_size);
        transformFunc->needInputDelta = true;
        printf("-- Attention Inner FC-2 ");
        transformFunc_bp = new Fully_Conn_Layer<Sigmoid>(transformFunc, _hidden_size, 1);
    }
    Attention_Unit() = delete;
    
    ~Attention_Unit() {
        delete transformFunc_bp;
        delete transformFunc;
    }
    
    // Attention input data should be data concating rnn encoder output sequence, rather than one cell's output
    vector<float>& forward(const vector<Matrix*>& prevLOutputMatrix) {
        assert(prevLOutputMatrix.size() == batch_size);
        
        // init threadlocal var
        MatrixArr& input = *tl_input;
        input.arr.resize(batch_size);
        Matrix& attentionOutput = *tl_attentionOutput;
        attentionOutput.reset(1, dimension);
        
        Matrix& fc_output_act = *tl_fc_output_act;
        fc_output_act.reset(1, batch_size);
        
        Matrix* cache = NULL;
        
        vector<Matrix*>& wrapper = *tl_wrapper;
        wrapper.resize(1);
        
        FOR(idx, prevLOutputMatrix.size()) {
            input.arr[idx] = prevLOutputMatrix[idx]->copy(input.arr[idx]); // 1xD
            assert(input.arr[idx]->size() == dimension);
            wrapper[0] = input.arr[idx];
            auto res = transformFunc->forward(wrapper);
            assert(res.size() == 1);
            *fc_output_act.getEle(0, idx) = res[0];
        }
        // Softmax normalization
        softmax.forward(fc_output_act.pointer()->data(), fc_output_act.size());
        
        attentionOutput.zeroInit();
        FOR(idx, prevLOutputMatrix.size()) {
            cache = input.arr[idx]->copy(cache)->scale(*fc_output_act.getEle(0, idx));
            attentionOutput.add(cache);
        }
        delete cache;
        return attentionOutput.reference();
    }
    
    void backward(const vector<Matrix*>& outputDeltaMatrix) {
        Matrix* outputDelta = outputDeltaMatrix[0];
        assert(outputDelta->size() == this->output_dimension);
        
        // init threadlocal var
        MatrixArr& input = *tl_input;
        Matrix& fc_output_act = *tl_fc_output_act;
        
        vector<Matrix*>& wrapper = *tl_wrapper;
        vector<float>& scaleDelta = *tl_scaleDelta;
        scaleDelta.resize(batch_size);
        MatrixArr& input_delta = *tl_input_delta;
        input_delta.arr.resize(batch_size);
        Matrix* cache_bp = new Matrix(1, 1);
        Matrix* cache = NULL;
        
        FOR(idx, input.arr.size()) {
            // update softmax_fc by delta of softmax_fc(X)
            auto res = input.arr[idx]->Multiply(cache_bp, outputDelta->transpose());
            outputDelta->transpose(); // recover
            assert(res->size() == 1);
            scaleDelta[idx] = *cache_bp->getEle(0, 0);
        }
        softmax.backward(scaleDelta.data(), fc_output_act.pointer()->data(),
                         scaleDelta.data(), scaleDelta.size());
        // update transformFunc
        FOR(idx, input.arr.size()) {
            *cache_bp->getEle(0, 0) = scaleDelta[idx];
            wrapper[0] = cache_bp;
            transformFunc_bp->backward(wrapper);
            // input delta of transformFunc
            const Matrix& delta = transformFunc->inputDelta();
            input_delta.arr[idx] = delta.copy(input_delta.arr[idx]);
        }
        // pass back delta of X
        FOR(idx, input.arr.size()) {
            cache = outputDelta->copy(cache)->scale(*fc_output_act.getEle(0, idx));
            input_delta.arr[idx]->add(cache);
        }
        delete cache_bp;
        delete cache;
    }
    
    const vector<Matrix*>& output() {
        Matrix& attentionOutput = *tl_attentionOutput;
        vector<Matrix*>& wrapper = *tl_wrapper;
        wrapper[0] = &attentionOutput;
        return wrapper;
    }
    const vector<Matrix*>& inputDelta() {
        MatrixArr& input_delta = *tl_input_delta;
        return input_delta.arr;
    }
    
    void applyBatchGradient() {
        transformFunc->applyBatchGradient();
        if (nextLayer) {
            nextLayer->applyBatchGradient();
        }
    }
    
private:
    Fully_Conn_Layer<Sigmoid> *transformFunc, *transformFunc_bp;
    Softmax softmax;
    size_t batch_size, dimension;
    
    ThreadLocal<MatrixArr> tl_input;
    ThreadLocal<Matrix> tl_attentionOutput;
    ThreadLocal<Matrix> tl_fc_output_act;
    
    ThreadLocal<vector<float> > tl_scaleDelta;
    ThreadLocal<MatrixArr> tl_input_delta;
    
    ThreadLocal<vector<Matrix*> > tl_wrapper;
};

#endif /* attention_unit_h */
