#include <unity.h>
#include <Arduino.h>

// Basic test to verify test framework is working
void test_basic_arithmetic()
{
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
    TEST_ASSERT_EQUAL_INT(0, 5 - 5);
    TEST_ASSERT_EQUAL_INT(25, 5 * 5);
}

void test_string_operations()
{
    String testString = "Hello";
    testString += " World";

    TEST_ASSERT_EQUAL_STRING("Hello World", testString.c_str());
    TEST_ASSERT_EQUAL_INT(11, testString.length());
    TEST_ASSERT_TRUE(testString.indexOf("World") > 0);
}

void test_boolean_logic()
{
    TEST_ASSERT_TRUE(true);
    TEST_ASSERT_FALSE(false);
    TEST_ASSERT_TRUE(1 == 1);
    TEST_ASSERT_FALSE(1 == 2);
}

void test_array_operations()
{
    int testArray[] = {1, 2, 3, 4, 5};
    int arraySize = sizeof(testArray) / sizeof(testArray[0]);

    TEST_ASSERT_EQUAL_INT(5, arraySize);
    TEST_ASSERT_EQUAL_INT(1, testArray[0]);
    TEST_ASSERT_EQUAL_INT(5, testArray[4]);
}

void test_memory_allocation()
{
    // Test basic memory operations
    char *buffer = (char *)malloc(100);
    TEST_ASSERT_NOT_NULL(buffer);

    strcpy(buffer, "Test");
    TEST_ASSERT_EQUAL_STRING("Test", buffer);

    free(buffer);
    TEST_ASSERT_TRUE(true); // If we get here, no crash occurred
}

void setup()
{
    delay(2000); // Wait for serial monitor

    UNITY_BEGIN();

    RUN_TEST(test_basic_arithmetic);
    RUN_TEST(test_string_operations);
    RUN_TEST(test_boolean_logic);
    RUN_TEST(test_array_operations);
    RUN_TEST(test_memory_allocation);

    UNITY_END();
}

void loop()
{
    // Empty - tests run once in setup()
}
