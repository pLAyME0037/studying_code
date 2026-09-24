import cv2

plate_cascade = cv2.CascadeClassifier('plate_number.xml')
cap = cv2.VideoCapture('video.mp4')

if not cap.isOpened():
    print("Cannot open video!")
    exit()

# Target width for detection to speed up processing
DETECTION_WIDTH = 640
FRAME_SKIP = 2  # Detect every 2nd frame
frame_count = 0
plates = []

while True:
    ret, frame = cap.read()
    if not ret:
        break

    frame_count += 1
    h_orig, w_orig = frame.shape[:2]

    # Only run detection every N frames
    if frame_count % FRAME_SKIP == 0:
        # Scale down for faster detection
        scale = DETECTION_WIDTH / float(w_orig)
        small_frame = cv2.resize(frame, (DETECTION_WIDTH, int(h_orig * scale)))
        gray = cv2.cvtColor(small_frame, cv2.COLOR_BGR2GRAY)

        # Detect on smaller image
        detected = plate_cascade.detectMultiScale(
            gray,
            scaleFactor=1.1,
            minNeighbors=5,
            minSize=(int(60 * scale), int(20 * scale))
        )

        # Scale bounding box coordinates back to original frame size
        plates = [(int(x / scale), int(y / scale), int(w / scale), int(h / scale)) 
                  for (x, y, w, h) in detected]

    # Draw results
    for i, (x, y, w, h) in enumerate(plates):
        cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)
        plate_roi = frame[y:y + h, x:x + w]

        # Only display preview if ROI is valid and fits within frame bounds
        if plate_roi.size > 0 and h_orig > 80 and w_orig > 210:
            enlarged = cv2.resize(plate_roi, (200, 70))
            frame[10:80, 10:210] = enlarged

    cv2.imshow('License Plate Detection', frame)

    # 1ms delay allows max playback speed while keeping UI responsive
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()
