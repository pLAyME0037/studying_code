#!/bin/bash
# Test script for clinic endpoints
# Usage: ./test_endpoints.sh

set -e

BASE="http://localhost:8000"
PASSED=0
FAILED=0

test_endpoint() {
    local name="$1"
    local method="$2"
    local url="$3"
    local data="$4"
    local expected="$5"

    echo -n "Testing $name... "
    if [ "$method" = "GET" ]; then
        code=$(curl -s -o /dev/null -w "%{http_code}" "$url")
    else
        code=$(curl -s -X "$method" -d "$data" -o /dev/null -w "%{http_code}" "$url")
    fi

    if [ "$code" = "$expected" ]; then
        echo "PASS ($code)"
        ((PASSED++))
    else
        echo "FAIL (got $code, expected $expected)"
        ((FAILED++))
    fi
}

# Ensure server is running
echo "Checking server..."
if ! curl -s -o /dev/null -w "%{http_code}" "$BASE/patients" | grep -q "200"; then
    echo "Server not responding on $BASE"
    exit 1
fi

echo "=== Master Detail Pages ==="
test_endpoint "Patients list" "GET" "$BASE/patients" "" "200"
test_endpoint "Organizations list" "GET" "$BASE/organizations" "" "200"
test_endpoint "Medicine Import list" "GET" "$BASE/medicine-imports" "" "200"

echo ""
echo "=== Child Create (with query string FK) ==="
test_endpoint "Patient Daily Invoice create" "POST" "$BASE/patient-daily-invoices/create?patient_id=1" "invoice_date=2026-08-25&amount_in_riel=5000&amount_in_dollar=12" "302"
test_endpoint "Patient Medicine Invoice create" "POST" "$BASE/patient-medicine-invoices/create?patient_id=1" "invoice_date=2026-08-25&amount_in_riel=3000&amount_in_dollar=8" "302"
test_endpoint "Patient Invoice Out create" "POST" "$BASE/patient-invoice-out/create?patient_id=1" "room_id=1&room_price=100&start_date=2026-08-25&end_date=2026-08-27&room_day=2" "302"
test_endpoint "Appointment create" "POST" "$BASE/appointments/create?patient_id=1" "doctor_id=1&date=2026-08-25&time=10:00" "302"

test_endpoint "Org Daily Invoice create" "POST" "$BASE/organization-daily-invoices/create?organization_id=1" "invoice_date=2026-08-25&amount_in_riel=5000&amount_in_dollar=12" "302"
test_endpoint "Org Invoice create" "POST" "$BASE/organization-invoices/create?organization_id=1" "invoice_date=2026-08-25&amount_in_riel=5000&amount_in_dollar=12" "302"
test_endpoint "Org Invoice Detail create" "POST" "$BASE/organization-invoice-details/create?organization_id=1" "medicine_id=1&qty=5&price=20&amount=100&currency=riel" "302"
test_endpoint "Org Payment create" "POST" "$BASE/organization-payments/create?organization_id=1" "organization_invoice_id=1&date=2026-08-25&price=100&currency=riel" "302"
test_endpoint "Org Invoice Out create" "POST" "$BASE/organization-invoice-out/create?organization_id=1&patient_id=1" "room_id=1&room_price=100&start_date=2026-08-25&end_date=2026-08-27&room_day=2" "302"

test_endpoint "Medicine Import create" "POST" "$BASE/medicine-imports/create" "date=2026-08-25&medicine_id=1&qty=10&price=50" "302"

echo ""
echo "=== Child List in Master Detail ==="
test_endpoint "Patients with children" "GET" "$BASE/patients" "" "200"

echo ""
echo "=== CRUD Operations on Children ==="
# Get first child ID for edit/update/delete tests
echo "Note: Edit/Update/Delete tests require existing child IDs"

echo ""
echo "=== Summary ==="
echo "Passed: $PASSED"
echo "Failed: $FAILED"

if [ $FAILED -gt 0 ]; then
    exit 1
fi
