#!/bin/bash

URL="http://localhost:45803"
content=$(cat "UnitTest_1in.enc")

# Send the content of UnitTest.json to the server and capture the response body and HTTP status code
response=$(curl -s -w "\n%{http_code}" -X POST -H "Content-Type: application/octet-stream" -d "$content" $URL)

# Extract the HTTP status code from the response
http_code=$(echo "$response" | tail -n1)

# Extract the response body from the response
response_body=$(echo "$response" | sed '$d')

# Output the response body
echo "Response body: $response_body"

# Check if the status code is 200
if [ "$http_code" -ne 200 ]; then
    echo "Test failed: HTTP status code $http_code"
    exit 1
fi

echo "Test passed"
exit 0