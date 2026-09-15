import cv2

plate_cascade = cv2.CascadeClassifier('plate_number.xml')

cap = cv2.VideoCapture('video.mp4')

if not cap.isOpened():
    print("Can not open vidio!")
    exit()

while True:
    ret, frame = cap.read()
    if not ret:
        break

    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    plates = plate_cascade.detectMultiScale(
        gray,
        scaleFactor=1.1,
        minNeighbors=5,
        minSize=(60, 20)
    )

    for (x, y, w, h) in plates:
        cv2.rectangle(frame, (x, y), (x + w, y + h), (255, 0, 0), 2)

        plate_roi = frame[y:y + h, x:x + w]

        if plate_roi.size > 0:
            enlarged = cv2.resize(plate_roi, (200, 70))
            frame[10:80, 10:210] = enlarged

    cv2.imshow('License Plate Detection', frame)

    if cv2.waitKey(25) & 0xFF == ord('q'):
        break

cap.release()
cv2.destroyAllWindows()