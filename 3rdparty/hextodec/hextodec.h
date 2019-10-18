#include <string>

class BaseConverter
{
public:
    std::string GetSourceBaseSet() const { return sourceBaseSet_; }
    std::string GetTargetBaseSet() const { return targetBaseSet_; }
    unsigned int GetSourceBase() const { return (unsigned int)sourceBaseSet_.length(); }
    unsigned int GetTargetBase() const { return (unsigned int)targetBaseSet_.length(); }

    BaseConverter(const std::string& sourceBaseSet, const std::string& targetBaseSet);

    static const BaseConverter& DecimalToBinaryConverter();

    static const BaseConverter& BinaryToDecimalConverter();
    static const BaseConverter& DecimalToHexConverter();
    static const BaseConverter& HexToDecimalConverter();

    std::string  Convert(std::string value) const;


    std::string Convert(const std::string& value, size_t minDigits) const;

    std::string FromDecimal(unsigned int value) const;

    std::string FromDecimal(unsigned int value, size_t minDigits) const;

    unsigned int ToDecimal(std::string value) const;

private:
    static unsigned int divide(const std::string& baseDigits, 
                               std::string& x, 
                               unsigned int y);

    static unsigned int base2dec(const std::string& baseDigits,
                                 const std::string& value);

    static std::string dec2base(const std::string& baseDigits, unsigned int value);

private:
    static const char*  binarySet_;
    static const char*  decimalSet_;
    static const char*  hexSet_;
    std::string         sourceBaseSet_;
    std::string         targetBaseSet_;
};