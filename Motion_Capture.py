import cv2
import mediapipe as mp
import numpy as np
import socket
import struct

# --- 配置 UDP ---
ESP_IP = "192.168.1.100"  # 【重要】改成你 ESP32 串口打印出来的 IP
ESP_PORT = 8888
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# --- MediaPipe 初始化 ---
mp_pose = mp.solutions.pose
pose = mp_pose.Pose(min_detection_confidence=0.7, min_tracking_confidence=0.7)
mp_drawing = mp.solutions.drawing_utils

cap = cv2.VideoCapture(0)  # 打开摄像头


# --- 计算角度的辅助函数 ---
def calculate_angle(a, b, c):
    """计算三个点之间的夹角 (比如 肩-肘-腕 计算肘部角度)"""
    a = np.array(a)  # First
    b = np.array(b)  # Mid
    c = np.array(c)  # End

    radians = np.arctan2(c[1] - b[1], c[0] - b[0]) - np.arctan2(a[1] - b[1], a[0] - b[0])
    angle = np.abs(radians * 180.0 / np.pi)

    if angle > 180.0:
        angle = 360 - angle
    return angle


while cap.isOpened():
    ret, frame = cap.read()
    if not ret: break

    # 镜像翻转，方便自己看
    frame = cv2.flip(frame, 1)
    rgb_frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    results = pose.process(rgb_frame)

    base_angle = 90
    shoulder_angle = 90
    elbow_angle = 90
    claw_angle = 0  # 默认张开

    if results.pose_landmarks:
        landmarks = results.pose_landmarks.landmark

        # 获取关键点坐标 (这里以右臂为例: 12肩, 14肘, 16腕)
        # 如果你想用左臂，改成 11, 13, 15
        shoulder = [landmarks[12].x, landmarks[12].y]
        elbow = [landmarks[14].x, landmarks[14].y]
        wrist = [landmarks[16].x, landmarks[16].y]
        hip = [landmarks[24].x, landmarks[24].y]  # 髋部，用于辅助计算肩部抬起

        # 1. 计算肘部角度 (Elbow Angle)
        # 范围通常在 0-180 之间
        raw_elbow = calculate_angle(shoulder, elbow, wrist)
        elbow_angle = int(np.interp(raw_elbow, [30, 160], [180, 0]))  # 映射并反转

        # 2. 计算肩部角度 (Shoulder Angle) - 大臂抬起
        # 计算 髋-肩-肘 的夹角
        raw_shoulder = calculate_angle(hip, shoulder, elbow)
        shoulder_angle = int(np.interp(raw_shoulder, [15, 100], [180, 90]))

        # 3. 计算底座 (Base) - 简单方案：根据手腕在屏幕的X轴位置
        # 如果手在屏幕左边，底座左转；在右边，底座右转
        # wrist[0] 是 0.0 ~ 1.0
        base_angle = int(np.interp(wrist[0], [0.2, 0.8], [180, 0]))

        # --- 这里预留 EMG 信号处理 ---
        # if emg_signal > threshold: claw_angle = 90 (闭合)

        # 绘制骨架
        mp_drawing.draw_landmarks(frame, results.pose_landmarks, mp_pose.POSE_CONNECTIONS)

        # 发送 UDP 数据 (打包成 4个字节)
        # 限制范围 0-180
        packet = struct.pack('BBBB',
                             max(0, min(180, base_angle)),
                             max(0, min(180, shoulder_angle)),
                             max(0, min(180, elbow_angle)),
                             claw_angle)

        try:
            sock.sendto(packet, (ESP_IP, ESP_PORT))
        except Exception as e:
            print(e)

        # 显示数据
        cv2.putText(frame, f"Elbow: {int(elbow_angle)}", (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 2)

    cv2.imshow('Mediapipe Feed', frame)
    if cv2.waitKey(10) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()