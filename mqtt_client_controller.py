import asyncio

import paho.mqtt.client as mqtt
from evdev import InputDevice, categorize, ecodes, KeyEvent
import time
import numpy as np

# CONSTANTS
USE_SAME_STICK = 1

CL_N = {
    1: 'Primadonna',
    2: 'SAM Super Automated Machine',
    3: 'LED-Brille',
    4: 'LED-Schlange',
}

T_COLOR_HSV = 'color_hsv'
T_COLOR_HSV_ADD = 'color_hsv/add'
T_COLOR_HSV_MAX = 'color_hsv/max'
T_MODE = 'mode'
T_MODE_FRAME = 'mode/next_frame'
T_MODE_PAUSE = 'mode/pause'
T_MODE_GO = 'mode/go'
T_MODE_SPEED = 'mode/speed'


MQTT_TIMER = 50  # ms
# INC_MAX_HUE = 3
# INC_MAX_SATURATION = 4
# INC_MAX_BRIGHTNESS = 10
# INC_MAX_SPEED = 30

MODES = {
    0: "M_STILL",
    1: "M_GRADIENT",
    21: "M_A_PRIDE",
    22: "M_A_DISCO",
    23: "M_A_PULSE",
    24: "M_A_SPEED_UP",
    25: "M_A_SCHNECKE",
}
MODES_LIST = list(MODES.keys())
selected_mode = 0

# VARIABLES
cl_l2 = 1
cl_l1 = 2

print("Starting up.")
print("Getting game pad")
gamepad = InputDevice('/dev/input/event20')
print(gamepad)

#  Controller Constants
MIN_MAX_FLAT = 1028 + 512  # Flat from edge
FLAT = 2048  # Flat from middle
SX = 1
SY = 0
CX = 4
CY = 3
CROSS_X = 17
CROSS_Y = 16
axis = gamepad.absinfo

# BUTTON FLAGS
cross_up = 0
cross_down = 0
cross_left = 0
cross_right = 0
r1_btn = 0
r2_btn = 0
l1_btn = 0
l2_btn = 0
screenshot_btn = 0
home_btn = 0
a_btn = 0
b_btn = 0
x_btn = 0
y_btn = 0
minus_btn = 0
plus_btn = 0
left_stick = 0

print("Calibrating STICK and CSTICK, do not touch.")
STI_X = STI_Y = CSTI_X = CSTI_Y = None
ms = time.time()
while True:
    tmp_x = axis(SX).value
    tmp_y = axis(SY).value
    tmp_cx = axis(CX).value
    tmp_cy = axis(CY).value
    if STI_X != tmp_x or STI_Y != tmp_y or CSTI_X != tmp_cx or CSTI_Y != tmp_cy:
        STI_X = tmp_x
        STI_Y = tmp_y
        CSTI_X = tmp_cx
        CSTI_Y = tmp_cy
        print("NO TOUCH!")
        time.sleep(0.25)
        ms = time.time()

    if ms + 2 < time.time():
        break



print("Thanks. Now flick those frikkin sticks around so I can find deadspace.")
ms = time.time() * 1000
STI_X_MIN = STI_Y_MIN = CSTI_X_MIN = CSTI_Y_MIN = 500000
STI_X_MAX = STI_Y_MAX = CSTI_X_MAX = CSTI_Y_MAX = 0
while True:
    while (time.time() * 1000 - ms < 2000):
        sx = gamepad.absinfo(0).value
        sy = gamepad.absinfo(1).value
        cx = gamepad.absinfo(3).value
        cy = gamepad.absinfo(4).value
        STI_X_MAX = max(STI_X_MAX, sx)
        STI_Y_MAX = max(STI_Y_MAX, sy)
        CSTI_X_MAX = max(CSTI_X_MAX, cx)
        CSTI_Y_MAX = max(CSTI_Y_MAX, cy)
        STI_X_MIN = min(STI_X_MIN, sx)
        STI_Y_MIN = min(STI_Y_MIN, sy)
        CSTI_X_MIN = min(CSTI_X_MIN, cx)
        CSTI_Y_MIN = min(CSTI_Y_MIN, cy)
    if STI_X_MAX == STI_X_MIN or CSTI_X_MAX == CSTI_X_MIN or STI_Y_MAX == STI_Y_MIN or STI_Y_MAX == STI_Y_MIN:
        print("WHY WONT YOU FLICK!?!")
        ms = time.time() * 1000
    else:
        break

STI = (STI_X, STI_X_MIN, STI_X_MAX, STI_Y, STI_Y_MIN, STI_Y_MAX)
STI_LIN_X = np.append(np.append(np.linspace(STI_X_MIN + MIN_MAX_FLAT, STI_X - FLAT, 127), np.linspace(STI_X - FLAT, STI_X + FLAT, 1)), np.linspace(STI_X + FLAT, STI_X_MAX - MIN_MAX_FLAT, 127))
STI_LIN_Y = np.append(np.append(np.linspace(STI_Y_MIN + MIN_MAX_FLAT, STI_Y - FLAT, 127), np.linspace(STI_Y - FLAT, STI_Y + FLAT, 1)), np.linspace(STI_Y + FLAT, STI_Y_MAX - MIN_MAX_FLAT, 127))
CSTI = (CSTI_X, CSTI_X_MIN, CSTI_X_MAX, CSTI_Y, CSTI_Y_MIN, CSTI_Y_MAX)
CSTI_LIN_X = np.append(np.append(np.linspace(CSTI_X_MIN + MIN_MAX_FLAT, CSTI_X - FLAT, 127), np.linspace(CSTI_X - FLAT, CSTI_X + FLAT, 1)), np.linspace(CSTI_X + FLAT, CSTI_X_MAX - MIN_MAX_FLAT, 127))
CSTI_LIN_Y = np.append(np.append(np.linspace(CSTI_Y_MIN + MIN_MAX_FLAT, CSTI_Y - FLAT, 127), np.linspace(CSTI_Y - FLAT, CSTI_Y + FLAT, 1)), np.linspace(CSTI_Y + FLAT, CSTI_Y_MAX - MIN_MAX_FLAT, 127))
print("Controller calibrated.")

print("Connecting to mqtt Server")
client = mqtt.Client("controller")
while client.connect("192.168.1.1", 1883) != 0:
    print("Could not connect. Trying again in 2 sec")
    time.sleep(2)
client.loop_start()
mqtt_timer = {}

def publish(receiver, topic, msg="", ignore_timer=False):
    global mqtt_timer
    timer_rec = mqtt_timer.get(receiver, 0)

    if not ignore_timer and time.time() - timer_rec < MQTT_TIMER / 1000:
        return

    mqtt_timer[receiver] = time.time()
    client.publish('{}/{}'.format(receiver, topic), msg)


def conv_stick_to_8bit(val, lin_space, neg=False):
    idx = np.digitize(val, lin_space).item()
    if neg:
        return 255 - idx
    else:
        return idx


def conv_8bit_to_inc(val_8bit, inc_range):
    lin_space = np.linspace(0, 255, 2*inc_range)
    return np.digitize(val_8bit, lin_space).item() - inc_range


def get_degress_255(x, y, lin_space_x, lin_space_y):
    sin_t_val = conv_stick_to_8bit(x, lin_space_x, neg=False)
    cos_t_val = conv_stick_to_8bit(y, lin_space_y)
    sin_t = (sin_t_val - 128) / 128
    cos_t = (cos_t_val - 128) / 128

    one = sin_t*sin_t + cos_t*cos_t
    if one < 0.7:
        return -1

    sin_t_scaled = np.sqrt(sin_t * sin_t / one) * np.sign(sin_t)
    cos_t_scaled = np.sqrt(cos_t * cos_t / one) * np.sign(cos_t)

    u = -1
    if sin_t_scaled <= 0 and cos_t_scaled >= 0:
        u = np.arctan(np.abs(cos_t_scaled / sin_t_scaled)) + np.pi * 0
    elif sin_t_scaled >= 0 and cos_t_scaled >= 0:
        u = np.arctan(np.abs(sin_t_scaled / cos_t_scaled)) + np.pi * 0.5
    elif sin_t_scaled >= 0 and cos_t_scaled <= 0:
        u = np.arctan(np.abs(cos_t_scaled / sin_t_scaled)) + np.pi * 1
    elif sin_t_scaled <= 0 and cos_t_scaled <= 0:
        u = np.arctan(np.abs(sin_t_scaled / cos_t_scaled)) + np.pi * 1.5
    if u == -1: return -1

    rad = u / np.pi
    deg = np.uint8(np.digitize(rad,np.linspace(0.15,1.85, 255)))
    return int(deg)


def reset_leds(receiver):
    publish(receiver, T_MODE, 0, ignore_timer=True)
    publish(receiver, T_COLOR_HSV, '255;255;0', ignore_timer=True)
    publish(receiver, T_MODE_SPEED, '700', ignore_timer=True)
    publish(receiver, T_MODE_GO, 1, ignore_timer=True)


def set_hue(receiver, X, Y, LIN_X, LIN_Y):
    hue_255 = get_degress_255(axis(X).value, axis(Y).value, LIN_X, LIN_Y)
    publish(receiver, T_COLOR_HSV, "{}".format(hue_255))


def set_brightness(receiver, X, Y, LIN_X, LIN_Y, stick_btn):
    sat_255 = get_degress_255(axis(X).value, axis(Y).value, LIN_X, LIN_Y)
    if stick_btn in gamepad.active_keys():
        publish(receiver, T_COLOR_HSV_MAX, '{}'.format(sat_255), ignore_timer=True)

    publish(receiver, T_COLOR_HSV, "-1;-1;{}".format(sat_255))

def set_speed(receiver, X, Y, LIN_X, LIN_Y):
    speed_255 = get_degress_255(axis(X).value, axis(Y).value, LIN_X, LIN_Y)
    speed = speed_255*(np.log2(speed_255)/np.log2(2.5))
    speed = 1 if np.isnan(speed) else speed
    speed = max(1, speed)
    speed = min(30000, speed)
    publish(receiver, T_MODE_SPEED, speed)

# def set_saturation(receiver):  # TODO


publish(cl_l2, T_MODE, '0', ignore_timer=True)
publish(cl_l1, T_MODE, '0', ignore_timer=True)
publish(cl_l2, T_COLOR_HSV, '132;255;0', ignore_timer=True)
publish(cl_l1, T_COLOR_HSV, '150;255;0', ignore_timer=True)
print("Waiting for events")
while True:
    event = gamepad.read_one()

    if event is None:
        continue

    ce = categorize(event)
    time.sleep(0.1) #  dunno, let's try minilag
    keys = gamepad.active_keys()

    if USE_SAME_STICK and 315 in keys:  # c-stick pressed
        set_speed('a', CX, CY, CSTI_LIN_X, CSTI_LIN_Y)

    if 314 in keys and left_stick == 0:  # left-stick pressed
        left_stick = 1
        cl_l2 = 1 if cl_l2 != 1 else 4
        cl_l1 = 3 if cl_l1 != 3 else 2
        print("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~")
        print("LEFT: {}\t RIGHT: {}".format(CL_N[cl_l1], CL_N[cl_l2]))
        print("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~")
    if 314 not in keys and left_stick == 1:
        left_stick = 0


    # todo l1 + l2?
    if 308 in keys:  # l1
        set_hue(cl_l1, SX, SY, STI_LIN_X, STI_LIN_Y)
    if 309 in keys:  # r1
        if USE_SAME_STICK:
            set_hue(cl_l2, SX, SY, STI_LIN_X, STI_LIN_Y)
        else:
            set_hue(cl_l2, CX, CY, CSTI_LIN_X, CSTI_LIN_Y)

    if 310 in keys:  # l2
        set_brightness(cl_l1, SX, SY, STI_LIN_X, STI_LIN_Y, stick_btn=314)
    if 311 in keys:  # r2
        r2_btn = 1
        if USE_SAME_STICK:
            set_brightness(cl_l2, SX, SY, STI_LIN_X, STI_LIN_Y, stick_btn=314)
        else:
            set_brightness(cl_l2, CX, CY, CSTI_LIN_X, CSTI_LIN_Y, stick_btn=315)

    if 312 in keys and minus_btn == 0:  # -
        reset_leds(cl_l1)
    elif 312 not in keys and minus_btn == 1:
        minus_btn = 0
    if 313 in keys and plus_btn == 0:  # + # TODO ist das +?
        reset_leds(cl_l2)
    elif 313 not in keys and plus_btn == 1:
        plus_btn = 0

    if 305 in keys and a_btn == 0:
        a_btn = 1
        publish(cl_l2, 'mode/next_frame')
    elif 305 not in keys and b_btn == 1:
        a_btn = 0
    if 304 in keys and b_btn == 0:
        b_btn = 1
        publish(cl_l1, 'mode/next_frame')
    elif 304 not in keys and b_btn == 1:
        b_btn = 0

    if 307 in keys and x_btn == 0:  # X stop
        publish('a', T_MODE_PAUSE)
        x_btn = 1
    elif 307 not in keys and x_btn == 1:
        x_btn = 0

    if 306 in keys and y_btn == 0:
        publish('a', T_MODE_GO)
        y_btn = 1
    elif 306 not in keys and y_btn == 1:
        y_btn = 0

    if 317 in keys and screenshot_btn == 0:  # Screenshot button
        screenshot_btn = 1
        publish(cl_l1, T_MODE, MODES_LIST[selected_mode])
    elif 317 not in keys and screenshot_btn == 1:
        screenshot_btn = 0
    if 316 in keys and screenshot_btn == 0:  # Home button
        home_btn = 1
        publish(cl_l2, T_MODE, MODES_LIST[selected_mode])
    elif 316 not in keys and screenshot_btn == 1:
        home_btn = 0

    if axis(CROSS_X).value == -1 and cross_up == 0:
        cross_up = 1
        selected_mode = MODES_LIST.index(0)
        print("Selected Mode: {} - Press SCREENSHOT to Send".format(MODES[0]))
    elif axis(CROSS_X).value != -1 and cross_up == 1:
        cross_up = 0

    if axis(CROSS_X).value == 1 and cross_down == 0:
        selected_mode = MODES_LIST.index(22)
        print("Selected Mode: {} - Press SCREENSHOT to Send".format(MODES[22]))
        cross_down = 1
    elif axis(CROSS_X).value != 1 and cross_down == 1:
        cross_down = 0

    if axis(CROSS_Y).value == -1 and cross_left == 0:
        cross_left = 1
        selected_mode -= 1
        selected_mode = selected_mode % len(MODES)
        print("Selected Mode: {} - Press SCREENSHOT to Send".format(MODES[MODES_LIST[selected_mode]]))
    elif axis(CROSS_Y).value != -1 and cross_left == 1:
        cross_left = 0

    if axis(CROSS_Y).value == 1 and cross_right == 0:
        cross_right = 1
        selected_mode += 1
        selected_mode = selected_mode % len(MODES)
        print("Selected Mode: {} - Press SCREENSHOT to Send".format(MODES[MODES_LIST[selected_mode]]))
    elif axis(CROSS_Y).value != -1 and cross_right == 1:
        cross_right = 0



