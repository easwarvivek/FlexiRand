#pragma once
//------------------------------------------------------------------------------
//
//   Copyright 2019-2020 Fetch.AI Limited
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
//------------------------------------------------------------------------------

#include "glow_dkg.hpp"

#include <iostream>
#include <cstddef>

namespace fetch {
namespace consensus {

template<class CryptoType>
class GlowDvrf : public GlowDkg<CryptoType> {
public:
  using MessagePayload = typename GlowDkg<CryptoType>::MessagePayload;
  using Signature = typename GlowDkg<CryptoType>::Signature;
  using PrivateKey = typename GlowDkg<CryptoType>::PrivateKey;
  using VerificationKey = typename GlowDkg<CryptoType>::VerificationKey;
  using GroupPublicKey = typename GlowDkg<CryptoType>::GroupPublicKey;
  using Pairing = typename GlowDkg<CryptoType>::Pairing;
  using Proof = typename CryptoType::Proof;

  using PrivateInput  = typename std::pair <Signature,Proof>;


   GlowDvrf(uint32_t committeeSize, uint32_t threshold) : GlowDkg<CryptoType>{committeeSize, threshold} {}

  virtual ~GlowDvrf() = default;

  static Proof proof(const VerificationKey &G, const MessagePayload &message, const VerificationKey &y,
                     const Signature &sig,
                     const PrivateKey &x) {
    Signature PH;
    PH.hashAndMap(message);

    PrivateKey r;
    r.random();
    VerificationKey com1, com2;
    com1.mult(G, r);
    com2.mult(PH, r);

    Proof pi;
    pi.first.setHashOf(G, PH, y, sig, com1, com2);
    PrivateKey localVar;
    localVar.mult(x, pi.first);
    pi.second.add(r, localVar);
    return pi;
  }

  /**
 * Verifies a signature
 *
 * @param y The public key share
 * @param message Message that was signed
 * @param sign Signature to be verified
 * @param G Generator used in DKG
 * @return
 */
  static bool verify(const VerificationKey &y, const MessagePayload &message, const Signature &sign,
                     const VerificationKey &G,
                     const Proof &proof) {
    Signature PH;
    PH.hashAndMap(message);

    VerificationKey tmp, c1, c2;
    PrivateKey tmps;
    tmps.negate(proof.first);
    c1.mult(G, proof.second);
    tmp.mult(y, tmps);
    c1.add(c1, tmp);
    c2.mult(PH, proof.second);
    tmp.mult(sign, tmps);
    c2.add(c2, tmp);

    PrivateKey ch_cmp;
    ch_cmp.setHashOf(G, PH, y, sign, c1, c2);

    return proof.first == ch_cmp;
  }

  /**
* Verifies a group signature
*
* @param y The public key share
* @param message Message that was signed
* @param sign Signature to be verified
* @param G Generator used in DKG
* @return
*/
  static bool verify(const GroupPublicKey &y, const MessagePayload &message, const Signature &sign,
                     const GroupPublicKey &G) {
    Pairing e1, e2;
    Signature PH;
    PH.hashAndMap(message);

    e1.map(sign, G);
    e2.map(PH, y);
    return e1 == e2;
  }


 static std::tuple<PrivateKey, VerificationKey, Signature, Proof>   genMaskedInput(const VerificationKey &G,const MessagePayload &message) {

    PrivateKey rand; 
    rand.random(); 

    Signature PH, sign; 
    VerificationKey  expRand;

    PH.hashAndMap(message); // H(x)
    sign.mult(PH, rand);    //  H(x)**r
    expRand.mult(G,rand);       //  G ** r

    // Generate zero-knowledge proof and then return it along with H(x)**r
    auto zkExp  = proof(G, message, expRand , sign, rand);
    //return std::make_pair(sign, zkExp);
    return std::make_tuple(rand, expRand, sign, zkExp);
  }


  static PrivateInput proofAfterPrivateInput(const VerificationKey &G, const MessagePayload &message, const VerificationKey &expRand, const Signature &maskedInput,
                     const Proof &zkproof, const PrivateKey &x, const VerificationKey &y) {

    auto check =  verify(expRand, message, maskedInput, G, zkproof);
    //if (check){
        Signature mySign;
        mySign.mult(maskedInput,x);

        auto myPI = proofOfMasked(G, maskedInput, y, mySign, x);
        return std::make_pair(mySign, myPI);
     //   }
    /*
    else{
        return; 
        }
     */   
    }



    static Proof proofOfMasked(const VerificationKey &G, const Signature &maskedInput, const VerificationKey &y,
                     const Signature &sig,
                     const PrivateKey &x) {
        PrivateKey r;

        r.random();
        VerificationKey com1, com2;

        com1.mult(G, r);
        com2.mult(maskedInput, r);

        Proof pi;
        pi.first.setHashOf(G, maskedInput, y, sig, com1, com2);
        PrivateKey localVar;
        localVar.mult(x, pi.first);
        pi.second.add(r, localVar);

        return pi;

  }


  static Signature unMask(const Signature &sig, const PrivateKey rand){
    PrivateKey tmp;
    Signature finalVal;
    tmp.negate(rand);
    finalVal.mult(sig,tmp);
    return finalVal;
  }

  static void exp(const VerificationKey &G){

    PrivateKey r;
    r.random();
    VerificationKey expG;
    expG.mult(G,r);
    return;
  }



  SignaturesShare getSignatureShare(const MessagePayload &message, uint32_t rank) override {
    std::lock_guard<std::mutex> lock(this->mutex_);
    auto mySign = this->sign(message, this->privateKey_);
    auto myPI = proof(this->G, message, this->publicKeyShares_[rank], mySign, this->privateKey_);

    //Sanity check: verify own signature
    this->groupSignatureManager_.addSignatureShares(message, {rank, mySign});
    assert(verify(this->publicKeyShares_[rank], message, mySign, this->G, myPI));
    auto piStr = std::make_pair(myPI.first.toString(), myPI.second.toString());
    return SignaturesShare{message, mySign.toString(), piStr};
  }

  bool addSignatureShare(const fetch::consensus::pb::Gossip_SignatureShare &share_msg, uint32_t minerIndex) override {
    assert(share_msg.has_share_pi());
    assert(share_msg.has_share_pi2());
    std::lock_guard<std::mutex> lock(this->mutex_);
    Signature sig;
    Proof pi;

    if (sig.assign(share_msg.share_sig()) && pi.first.assign(share_msg.share_pi()) &&
        pi.second.assign(share_msg.share_pi2()) &&
        verify(this->publicKeyShares_[minerIndex], share_msg.message(), sig, this->G, pi)) {
      this->groupSignatureManager_.addSignatureShares(share_msg.message(), {minerIndex, sig});
      return true;
    } else {
      return false;
    }
  }
};

}
}
