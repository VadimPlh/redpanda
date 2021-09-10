const {
  SimpleTransform,
  PolicyError,
  PolicyInjection
} = require("@vectorizedio/wasm-api");
const transform = new SimpleTransform();
/* Topics that fire the transform function */
transform.subscribe([["input", PolicyInjection.Stored]]);
/* The strategy the transform engine will use when handling errors */
transform.errorHandler(PolicyError.SkipOnFailure);
/* Auxiliar transform function for records */
const uppercase = (record) => {
  const newRecord = {
    ...record,
    value: record.value.map((char) => {
      if (char >= 97 && char <= 122) {
        return char - 32;
      } else {
        return char;
      }
    }),
  };
  return newRecord;
}
/* Transform function */
transform.processRecord((recordBatch) => {
  const result = new Map();
  const transformedRecord = recordBatch.map(({ header, records }) => {
    return {
      header,
      records: records.map(uppercase),
    };
  });
  result.set("output", transformedRecord);
  // processRecord function returns a Promise
  return Promise.resolve(result);
});
exports["default"] = transform;
