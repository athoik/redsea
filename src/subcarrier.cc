#include "subcarrier.h"

#include <complex>
#include <deque>
#include <iostream>

#include "liquid_wrappers.h"

namespace redsea {

namespace {

const float kFs = 228000.0f;
const float kFc_0 = 57000.0f;
const int kInputBufferSize = 4096;
const int kSamplesPerSymbol = 4;

}


DeltaDecoder::DeltaDecoder() : prev_(0) {

}

DeltaDecoder::~DeltaDecoder() {

}

unsigned DeltaDecoder::decode(unsigned d) {
  unsigned bit = (d != prev_);
  prev_ = d;
  return bit;
}

Subcarrier::Subcarrier() : numsamples_(0),
  bit_buffer_(),
  fir_lpf_(511, 2100.0f / kFs),
  is_eof_(false),
  agc_(0.001f),
  nco_approx_(kFc_0 * 2 * M_PI / kFs),
  nco_exact_(0.0f),//FC_0 * 2 * PI_f / FS),
  symsync_(LIQUID_FIRFILT_RRC, kSamplesPerSymbol, 5, 0.5f, 32),
  modem_(LIQUID_MODEM_PSK2),
  sym_clk_(0),
  biphase_(0), prev_biphase_(0), delta_decoder_()
  {

    symsync_.setBandwidth(0.02f);
    symsync_.setOutputRate(1);
    nco_exact_.setPLLBandwidth(0.0004f);

}

Subcarrier::~Subcarrier() {

}

void Subcarrier::demodulateMoreBits() {

  int16_t sample[kInputBufferSize];
  int samplesread = fread(sample, sizeof(int16_t), kInputBufferSize, stdin);
  if (samplesread < kInputBufferSize) {
    is_eof_ = true;
    return;
  }

  for (int i = 0; i < samplesread; i++) {

    std::complex<float> sample_baseband = nco_approx_.mixDown(sample[i]);

    fir_lpf_.push(sample_baseband);
    std::complex<float> sample_lopass_unnorm = fir_lpf_.execute();

    std::complex<float> sample_lopass = agc_.execute(sample_lopass_unnorm);

    sample_lopass = nco_exact_.mixDown(sample_lopass);

    if (numsamples_ % (96 / kSamplesPerSymbol) == 0) {
      std::vector<std::complex<float>> y;
      y = symsync_.execute(sample_lopass);
      for (auto sy : y) {
        biphase_ = modem_.demodulate(sy);
        nco_exact_.stepPLL(modem_.getPhaseError());

        if (sym_clk_ == 1) {
          if (prev_biphase_ == 0 && biphase_ == 1) {
            bit_buffer_.push_back(delta_decoder_.decode(1));
          } else if (prev_biphase_ == 1 && biphase_ == 0) {
            bit_buffer_.push_back(delta_decoder_.decode(0));
          } else {
            sym_clk_ = 0;
          }
        }

        prev_biphase_ = biphase_;

        sym_clk_ = (sym_clk_ + 1) % 2;

      }
    }

    nco_approx_.step();

    numsamples_ ++;

  }

}

int Subcarrier::getNextBit() {
  while (bit_buffer_.size() < 1 && !isEOF())
    demodulateMoreBits();

  int bit = 0;

  if (bit_buffer_.size() > 0) {
    bit = bit_buffer_.front();
    bit_buffer_.pop_front();
  }

  return bit;
}

bool Subcarrier::isEOF() const {
  return is_eof_;
}

} // namespace redsea