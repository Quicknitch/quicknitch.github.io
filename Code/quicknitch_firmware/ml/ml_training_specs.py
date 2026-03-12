"""
ml_training_specs.py
====================
Quicknitch — ML Model Training Specifications
Covers all 5 models: wake word, person detection, gesture,
face recognition, safety scene.

Run sections independently or as a reference for the ML team.
Requires: TensorFlow 2.12+, tensorflow-model-optimization, librosa
"""

# ════════════════════════════════════════════════════════════════
# 1. WAKE WORD DETECTION — DS-CNN-S
# ════════════════════════════════════════════════════════════════

WAKE_WORD_SPEC = {
    "name": "DS-CNN-S Wake Word",
    "dataset": {
        "primary":   "Google Speech Commands v2 (35-class, 105k utterances)",
        "target_words": ["hey nitch", "quicknitch"],
        "negative":  "MUSAN noise + ESC-50 environmental sounds",
        "unknown":   "8 random GSC words as negative class",
        "split":     {"train": 0.80, "val": 0.10, "test": 0.10},
    },
    "input": {
        "sample_rate":    16000,
        "window_ms":      1000,
        "mel_bins":       40,
        "time_frames":    98,
        "hop_ms":         10,
        "window_ms_fft":  25,
        "n_fft":          512,
    },
    "augmentation": [
        "time_shift: ±100ms",
        "background_noise: SNR 0-30dB (MUSAN)",
        "speed_perturb: 0.9x-1.1x",
        "volume_perturb: ±6dB",
        "frequency_masking: F=10, 1 mask",
        "time_masking: T=25, 1 mask (SpecAugment)",
        "pitch_shift: ±2 semitones",
    ],
    "architecture": """
def build_ds_cnn_s(n_classes=10):
    import tensorflow as tf
    inp = tf.keras.Input(shape=(98, 40, 1), name='mel_input')
    x = tf.keras.layers.Conv2D(64, (3,3), padding='same', use_bias=False)(inp)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.ReLU()(x)
    for _ in range(4):
        x = tf.keras.layers.DepthwiseConv2D((3,3), padding='same',
                                             use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
        x = tf.keras.layers.ReLU()(x)
        x = tf.keras.layers.Conv2D(64, (1,1), padding='same',
                                    use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
        x = tf.keras.layers.ReLU()(x)
    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    out = tf.keras.layers.Dense(n_classes, activation='softmax')(x)
    return tf.keras.Model(inp, out, name='DS_CNN_S')
""",
    "training": {
        "optimizer":    "Adam(lr=1e-3, decay=1e-4)",
        "epochs":       100,
        "batch_size":   64,
        "lr_schedule":  "CosineDecay(1e-3, 100*steps_per_epoch)",
        "loss":         "sparse_categorical_crossentropy",
        "early_stop":   "val_accuracy patience=10",
    },
    "quantization": {
        "method":           "Quantization-Aware Training (QAT)",
        "qat_start_epoch":  70,
        "int8_activations": True,
        "int8_weights":     True,
        "representative_dataset": "1000 random train samples",
    },
    "targets": {
        "accuracy_top1":        ">= 95.0%",
        "false_accept_rate":    "< 1/hour (typical home SNR 20dB)",
        "false_reject_rate":    "< 5%",
        "confusion_matrix_req": "hey_nitch↔quicknitch confusion < 0.5%",
        "model_size_int8":      "< 50KB",
        "inference_time":       "< 30ms on ARM @ 400MHz",
    },
    "export_commands": """
# 1. Post-QAT TFLite export
converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type  = tf.int8
converter.inference_output_type = tf.int8

def representative_dataset():
    for x in train_dataset.batch(1).take(1000):
        yield [tf.cast(x[0], tf.float32)]

converter.representative_dataset = representative_dataset
tflite_model = converter.convert()
open('wake_word_int8.tflite', 'wb').write(tflite_model)

# 2. Convert to C array for TFLM
# xxd -i wake_word_int8.tflite > models/wake_word_model.h
# Rename array: g_wake_word_model, size: g_wake_word_model_size
""",
}


# ════════════════════════════════════════════════════════════════
# 2. PERSON DETECTION — MobileNetV1 0.25x
# ════════════════════════════════════════════════════════════════

PERSON_DETECT_SPEC = {
    "name": "Person Detector MobileNetV1-0.25",
    "dataset": {
        "primary":  "COCO 2017 (person class, ~64k images with person bbox)",
        "negative": "COCO images with no person (20k random subset)",
        "indoor_augment": "OpenImages V6 person subset (indoor scenes)",
        "split":    {"train": 0.85, "val": 0.10, "test": 0.05},
    },
    "input": {
        "size":     "96×96 grayscale",
        "norm":     "uint8 [0,255] → int8 [-128,127]",
    },
    "augmentation": [
        "random_horizontal_flip",
        "random_brightness: ±30",
        "random_contrast:   0.7-1.3",
        "random_crop:       80%-100% of image",
        "grayscale conversion from RGB (match mono sensor)",
        "random_jpeg_quality: 70-100",
        "mixup: alpha=0.2",
        "random_occlusion: up to 20% bbox area",
    ],
    "architecture": """
# MobileNetV1 with 0.25x width multiplier, modified for mono input
# Use TF-slim or keras_applications:
import tensorflow as tf
from tensorflow.keras.applications import MobileNet

base = MobileNet(input_shape=(96,96,1), alpha=0.25,
                 include_top=False, weights=None)
x = tf.keras.layers.GlobalAveragePooling2D()(base.output)
# Detection head: person prob + bbox (cx, cy, w, h)
out_person = tf.keras.layers.Dense(1, activation='sigmoid',
                                    name='person_prob')(x)
out_bbox   = tf.keras.layers.Dense(4, activation='sigmoid',
                                    name='bbox')(x)
model = tf.keras.Model(base.input, [out_person, out_bbox])
""",
    "loss": """
# Combined loss:
# L = BCE(person_label, person_prob) + lambda * L1(bbox_gt, bbox_pred)
# lambda = 5.0 when person present, 0 when absent
import tensorflow as tf
def detection_loss(y_true, y_pred):
    p_true, bbox_true = y_true[..., 0:1], y_true[..., 1:5]
    p_pred, bbox_pred = y_pred[0], y_pred[1]
    bce  = tf.keras.losses.binary_crossentropy(p_true, p_pred)
    bbox_loss = tf.reduce_mean(tf.abs(bbox_true - bbox_pred), axis=-1)
    has_person = tf.squeeze(p_true, -1)
    return bce + 5.0 * has_person * bbox_loss
""",
    "training": {
        "optimizer":  "SGD(lr=0.01, momentum=0.9, nesterov=True)",
        "epochs":     150,
        "batch_size": 128,
        "warmup_epochs": 5,
        "lr_schedule": "PiecewiseConstant: [60,120] → [1e-3, 1e-4]",
    },
    "quantization": {
        "method": "Post-Training INT8 with representative dataset",
        "representative_dataset": "500 COCO val images (96×96 grayscale)",
    },
    "targets": {
        "person_precision": ">= 92%",
        "person_recall":    ">= 90%",
        "bbox_iou":         ">= 0.65 (IoU@0.5 threshold)",
        "model_size_int8":  "< 80KB",
        "inference_time":   "< 40ms on HW-AccAI at 5fps",
    },
    "export_commands": """
converter = tf.lite.TFLiteConverter.from_saved_model('person_saved_model')
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type  = tf.int8
converter.inference_output_type = tf.int8
def rep_dataset():
    for img in val_images_grayscale_96[:500]:
        yield [img[None].astype('float32') / 127.5 - 1.0]
converter.representative_dataset = rep_dataset
tflite_model = converter.convert()
open('person_int8.tflite', 'wb').write(tflite_model)
# xxd -i person_int8.tflite > models/person_model.h
""",
}


# ════════════════════════════════════════════════════════════════
# 3. GESTURE RECOGNITION — Temporal CNN
# ════════════════════════════════════════════════════════════════

GESTURE_SPEC = {
    "name": "Temporal CNN Gesture Recognition",
    "dataset": {
        "primary":   "Custom Quicknitch gesture dataset (min 500 clips/class)",
        "secondary": "Jester dataset (subset: waving, pointing)",
        "classes":   ["wave_hello", "point_forward", "thumbs_up", "cross_arms_stop"],
        "clip_length": "15 frames @ 5fps (3-second clips)",
        "augmentation_required": True,
    },
    "input": {
        "type":       "Optical flow (dx, dy) at 48×48",
        "frames":     15,
        "channels":   2,
        "shape":      "(15, 48, 48, 2) → (15, 48*48*2) flattened for TCN",
    },
    "augmentation": [
        "temporal_jitter: ±2 frames",
        "spatial_flip: horizontal (mirror gestures)",
        "scale_jitter: 0.85-1.15 on flow magnitude",
        "gaussian_noise: σ=0.02 on flow",
        "random_temporal_crop from 20-frame clips",
        "background_person_mixup: paste gestures onto different scenes",
    ],
    "architecture": """
def build_temporal_cnn(n_classes=4):
    import tensorflow as tf
    # Input: (batch, 15, 48, 48, 2)
    inp = tf.keras.Input(shape=(15, 48, 48, 2))

    # Spatial feature extraction per frame (shared weights)
    spatial = tf.keras.Sequential([
        tf.keras.layers.Conv2D(32, (3,3), padding='same', activation='relu'),
        tf.keras.layers.MaxPool2D(2,2),
        tf.keras.layers.Conv2D(64, (3,3), padding='same', activation='relu'),
        tf.keras.layers.GlobalAveragePooling2D(),
    ], name='spatial')

    # Apply spatial encoder to each frame
    x = tf.keras.layers.TimeDistributed(spatial)(inp)  # (B, 15, 64)

    # Temporal CNN (1D convolutions over time)
    x = tf.keras.layers.Conv1D(128, 3, padding='causal', activation='relu')(x)
    x = tf.keras.layers.Conv1D(128, 3, padding='causal',
                                dilation_rate=2, activation='relu')(x)
    x = tf.keras.layers.Conv1D(128, 3, padding='causal',
                                dilation_rate=4, activation='relu')(x)
    x = tf.keras.layers.GlobalAveragePooling1D()(x)
    out = tf.keras.layers.Dense(n_classes, activation='softmax')(x)
    return tf.keras.Model(inp, out, name='TemporalCNN_Gesture')
""",
    "training": {
        "optimizer":  "Adam(lr=1e-3)",
        "epochs":     80,
        "batch_size": 32,
        "augment_on_the_fly": True,
        "class_weights": "balanced (equal weight each gesture)",
    },
    "targets": {
        "per_class_accuracy": ">= 90% for each gesture",
        "false_trigger_rate": "< 1% on non-gesture motion",
        "confusion_matrix":   "wave↔point confusion < 3%",
        "confidence_threshold_at_deploy": 0.85,
        "model_size_int8":    "< 64KB",
    },
    "data_collection_guide": """
Collection protocol for custom gesture dataset:
- 10 participants × 50 clips/gesture = 500 clips/class minimum
- Distances: 0.5m, 0.8m, 1.0m, 1.2m from camera
- Lighting: bright indoor, dim indoor, backlit
- Background: varied (kitchen, living room, office)
- Include: glasses wearers, different skin tones, left/right hand variations
- Record at 96×96 grayscale, 5fps; extract 15-frame clips
- Label with start/end frame of gesture peak
""",
}


# ════════════════════════════════════════════════════════════════
# 4. FACE RECOGNITION — MobileFaceNet
# ════════════════════════════════════════════════════════════════

FACE_RECOG_SPEC = {
    "name": "MobileFaceNet INT8 (128-dim embedding)",
    "dataset": {
        "training":    "MS-Celeb-1M (cleaned) or LFW + VGGFace2",
        "fine_tune":   "Indoor lighting subset (match robot camera conditions)",
        "augmentation_critical": "Simulate grayscale conversion, HM01B0 noise",
    },
    "input": {
        "size":   "96×96 grayscale",
        "norm":   "[-1, 1] float32 during training; int8 [-128,127] at inference",
    },
    "augmentation": [
        "random_horizontal_flip",
        "random_brightness: ±20",
        "random_contrast: 0.8-1.2",
        "random_rotation: ±15 degrees",
        "random_crop_face: bbox expansion 5-15%",
        "gaussian_blur: σ=0.5-1.5 (simulate camera defocus)",
        "jpeg_compression: quality 70-95",
        "grayscale simulation: desaturate RGB",
        "shot_noise: Poisson (simulate low-light sensor noise)",
    ],
    "loss": """
# ArcFace loss (Additive Angular Margin) — best for face recognition
import tensorflow as tf
class ArcFaceLoss(tf.keras.layers.Layer):
    def __init__(self, n_classes, margin=0.5, scale=64.0):
        super().__init__()
        self.m = margin
        self.s = scale
        self.W = self.add_weight(shape=(128, n_classes),
                                  initializer='glorot_uniform',
                                  trainable=True)
    def call(self, embeddings, labels):
        W_norm = tf.math.l2_normalize(self.W, axis=0)
        e_norm = tf.math.l2_normalize(embeddings, axis=1)
        cos_t  = tf.matmul(e_norm, W_norm)
        cos_t  = tf.clip_by_value(cos_t, -1+1e-7, 1-1e-7)
        theta  = tf.acos(cos_t)
        # Add margin to target class
        one_hot = tf.one_hot(labels, depth=tf.shape(self.W)[1])
        target  = tf.cos(theta + self.m)
        logits  = self.s * (one_hot * target + (1-one_hot) * cos_t)
        return tf.keras.losses.sparse_categorical_crossentropy(
            labels, logits, from_logits=True)
""",
    "architecture": """
# MobileFaceNet — lightweight FaceNet variant
def build_mobilefacenet():
    import tensorflow as tf
    inp = tf.keras.Input(shape=(96, 96, 1))
    x = tf.keras.layers.Conv2D(64, 3, strides=2, padding='same',
                                use_bias=False)(inp)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.PReLU()(x)
    # 5 depthwise bottleneck blocks (abbreviated)
    for filters, stride in [(64,2),(128,2),(256,2),(512,1),(512,1)]:
        shortcut = x
        x = tf.keras.layers.DepthwiseConv2D(3, padding='same',
                                             use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
        x = tf.keras.layers.PReLU()(x)
        x = tf.keras.layers.Conv2D(filters, 1, strides=stride,
                                    use_bias=False)(x)
        x = tf.keras.layers.BatchNormalization()(x)
    # Linear GDConv
    x = tf.keras.layers.DepthwiseConv2D((3,3), padding='valid',
                                         use_bias=False)(x)
    x = tf.keras.layers.BatchNormalization()(x)
    x = tf.keras.layers.Flatten()(x)
    embedding = tf.keras.layers.Dense(128, use_bias=False,
                                       name='embedding')(x)
    embedding = tf.keras.layers.BatchNormalization(name='bn_embed')(embedding)
    return tf.keras.Model(inp, embedding, name='MobileFaceNet')
""",
    "training": {
        "optimizer":   "SGD(lr=0.1, momentum=0.9)",
        "epochs":      40,
        "batch_size":  128,
        "lr_schedule": "[10,20,30] → [0.01, 0.001, 0.0001]",
    },
    "evaluation": {
        "lfw_accuracy_target":    ">= 97%",
        "l2_threshold":           0.75,
        "enrollment_frames":      10,
        "recognition_fps":        2,
        "false_acceptance_rate":  "< 0.1%",
        "false_rejection_rate":   "< 5%",
    },
}


# ════════════════════════════════════════════════════════════════
# 5. SAFETY SCENE DETECTION
# ════════════════════════════════════════════════════════════════

SAFETY_SPEC = {
    "name": "Safety Scene CNN (5-class)",
    "classes": {
        0: "safe_indoor",
        1: "near_glass (window, glass door, mirror)",
        2: "staircase_detected",
        3: "too_dark (lux < 30)",
        4: "outdoor_detected",
    },
    "dataset": {
        "safe_indoor":  "MIT Indoor 67 (subset, 2000 images)",
        "near_glass":   "Custom + COCO glass/window crops (1000 images)",
        "staircase":    "ETHZ Staircase dataset + custom (1500 images)",
        "too_dark":     "LOL dataset (low-light) + artificially darkened",
        "outdoor":      "SUN397 outdoor subset (2000 images)",
        "split":        {"train": 0.80, "val": 0.10, "test": 0.10},
    },
    "input": {
        "size": "64×64 grayscale",
        "norm": "int8 [-128,127]",
    },
    "augmentation": [
        "random_horizontal_flip",
        "brightness_jitter: ±40",
        "contrast_jitter: 0.6-1.5",
        "random_rotation: ±10 degrees",
        "random_crop: 85-100%",
        "gaussian_noise: σ=5",
    ],
    "architecture": """
def build_safety_cnn(n_classes=5):
    import tensorflow as tf
    inp = tf.keras.Input(shape=(64,64,1))
    x = tf.keras.layers.Conv2D(32, 3, padding='same', activation='relu')(inp)
    x = tf.keras.layers.MaxPool2D(2,2)(x)  # 32×32
    x = tf.keras.layers.DepthwiseConv2D(3, padding='same',
                                         activation='relu')(x)
    x = tf.keras.layers.Conv2D(64, 1, activation='relu')(x)
    x = tf.keras.layers.MaxPool2D(2,2)(x)  # 16×16
    x = tf.keras.layers.DepthwiseConv2D(3, padding='same',
                                         activation='relu')(x)
    x = tf.keras.layers.Conv2D(128, 1, activation='relu')(x)
    x = tf.keras.layers.GlobalAveragePooling2D()(x)
    x = tf.keras.layers.Dropout(0.3)(x)
    out = tf.keras.layers.Dense(n_classes, activation='softmax')(x)
    return tf.keras.Model(inp, out, name='SafetyCNN')
""",
    "training": {
        "optimizer":  "Adam(lr=5e-4)",
        "epochs":     60,
        "batch_size": 64,
        "class_weights": "inverse frequency (staircase/glass overweight ×3)",
    },
    "targets": {
        "staircase_recall":   ">= 97% (safety critical)",
        "near_glass_recall":  ">= 95%",
        "overall_accuracy":   ">= 90%",
        "false_alarm_rate":   "< 2% (safe_indoor classified as alert)",
        "model_size_int8":    "< 48KB",
        "inference_time":     "< 20ms at 3fps",
    },
}


# ════════════════════════════════════════════════════════════════
# Utility: Print all model budgets
# ════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    specs = [
        (WAKE_WORD_SPEC,      "Wake Word"),
        (PERSON_DETECT_SPEC,  "Person Detect"),
        (GESTURE_SPEC,        "Gesture"),
        (FACE_RECOG_SPEC,     "Face Recog"),
        (SAFETY_SPEC,         "Safety Scene"),
    ]
    print(f"{'Model':<20} {'Size Budget':<15} {'Key Accuracy Target'}")
    print("-" * 65)
    for spec, name in specs:
        size = spec.get("targets", {}).get("model_size_int8", "—")
        tgt  = next(iter(
            {k:v for k,v in spec.get("targets",{}).items()
             if "accuracy" in k or "recall" in k}.values()
        ), "—")
        print(f"{name:<20} {size:<15} {tgt}")
