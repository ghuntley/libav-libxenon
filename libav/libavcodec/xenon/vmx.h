/*
 * File:   vmx.h
 * Author: cc
 *
 * Created on 1 novembre 2011, 10:00
 */

#ifndef VMX_H
#define	VMX_H

#ifdef	__cplusplus
extern "C" {
#endif

#undef vec_mladd
#undef vec_mulo
#undef vec_mule

    
    #define vec_s16l_to_fp(vec) vec_ctf(vec_unpackl(vec), 0)
    #define vec_s16h_to_fp(vec) vec_ctf(vec_unpackh(vec), 0)
    #define vec_fp_to_s16(vec) vec_pack(vec_cts(vec, 0), vec_cts(vec, 0))

    /**
     * vec_mladd existe pas en altivec
     * pack le short en int // 4 cycles
     * convert le int en float // 12 cycles
     * utilise vec_mladdfp pour l'op // 12
     * reconvert en int // 12 cycles
     * unpack en short // 4 cyles
     * total = (3 * 4 + 12 * 3) + 12 + (3 * 4 + 12 * 3) => 108 !!!
     **/

    /**
     * vector multiply-low and add modulo
     */
    static inline vec_s16 vec_mladd(vec_s16 vA, vec_s16 vB, vec_s16 vC) {
        //

        return vec_fp_to_s16(
                vec_madd(
                vec_s16l_to_fp(vA),
                vec_s16l_to_fp(vB),
                vec_s16l_to_fp(vC)
                )
                );
    }

    /**
     * vmulesh
     * Vector Multiply Even Integer
     * Each element of the result is the product of the corresponding high half-width elements of arg1 and arg2.
     */
    static inline vec_s32 vec_mule(vec_s16 vA, vec_s16 vB) {
        return vec_cts(
                vec_madd(
                vec_s16h_to_fp(vA),
                vec_s16h_to_fp(vB),
                (vec_f) vec_splat_s32(0)
                )
                , 0
                );
    }

    /**
     * vmulosh
     * Vector Multiply Odd Integer
     * Each element of the result is the product of the corresponding low half-width elements of arg1 and arg2.
     */
    static inline vec_s32 vec_mulo(vec_s16 vA, vec_s16 vB) {

        return vec_cts(
                vec_madd(
                vec_s16l_to_fp(vA),
                vec_s16l_to_fp(vB),
                (vec_f) vec_splat_s32(0)
                ), 0
                );
    }

#ifdef	__cplusplus
}
#endif

#endif	/* VMX_H */

