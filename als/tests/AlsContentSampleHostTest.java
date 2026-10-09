/* Copyright (C) 2026 The Avium Project
 * SPDX-License-Identifier: Apache-2.0
 */
package com.android.server.display;

import java.util.Arrays;
import java.util.Random;

/** Runs on a host JDK using the production estimator, without Android stubs. */
public final class AlsContentSampleHostTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) {
        int[] pixels = new int[1600];
        check(Arrays.equals(AlsContentSample.rgb(pixels, 0), new int[] {0, 0, 0}), "black");
        Arrays.fill(pixels, 0xffffffff);
        check(Arrays.equals(AlsContentSample.rgb(pixels, 0), new int[] {255, 255, 255}), "white");
        Arrays.fill(pixels, 0xff808080);
        check(Arrays.equals(AlsContentSample.rgb(pixels, 0), new int[] {128, 128, 128}), "gray");
        Arrays.fill(pixels, 0xff000000);
        pixels[0] = 0xffffffff;
        check(Arrays.equals(AlsContentSample.rgb(pixels, 0), new int[] {13, 13, 13}), "one white");
        Arrays.fill(pixels, 0xffffffff);
        pixels[0] = 0xff000000;
        check(Arrays.equals(AlsContentSample.rgb(pixels, 0), new int[] {217, 217, 217}), "one black");
        Arrays.fill(pixels, 0xff000000);
        pixels[0] = 0xffff0000;
        pixels[13] = 0xff00ff00;
        pixels[26] = 0xff0000ff;
        check(Arrays.equals(AlsContentSample.rgb(pixels, 0), new int[] {13, 13, 13}),
                "channels must sort independently");

        int[][] crops = {
            {555, 172, 595, 212}, {172, 621, 212, 661},
            {621, 2476, 661, 2516}, {2476, 555, 2516, 595}
        };
        for (int r = 0; r < 4; r++) {
            check(Arrays.equals(AlsContentSample.crop(1216, 2688, r), crops[r]), "crop " + r);
        }
        check(AlsContentSample.mode(0, 2) == 72, "mode 72");
        check(AlsContentSample.mode(1, -1) == 10, "invalid ACL");
        check(AlsContentSample.mode(2, 0) == 20, "default mode");
        check(AlsContentSample.mode(3, 3) == 33, "mode 33");
        check(AlsContentSample.mode(99, 4) == 20, "unknown mode");

        Random random = new Random(809);
        for (int trial = 0; trial < 1000; trial++) {
            for (int i = 0; i < pixels.length; i++) pixels[i] = random.nextInt() | 0xff000000;
            int[] expected = AlsContentSample.rgb(pixels, 0);
            // Rotate a real image one quarter turn at a time. Recover the same natural grid,
            // including the non-symmetric inner 6/19/32 sample coordinates.
            int[] current = pixels.clone();
            for (int r = 1; r < 4; r++) {
                int[] rotated = new int[1600];
                for (int y = 0; y < 40; y++) {
                    for (int x = 0; x < 40; x++) rotated[(39 - x) * 40 + y] = current[y * 40 + x];
                }
                current = rotated;
                check(Arrays.equals(AlsContentSample.rgb(current, r), expected), "rotation " + r);
            }
        }
        System.out.println("PASS: golden stock RGB cases, mode mapping, four crops, 3000 rotated images");
    }
}
