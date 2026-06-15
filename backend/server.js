const mqtt = require("mqtt");
const { MongoClient } = require("mongodb");

// ------------------- MQTT Setup -------------------
const MQTT_URL = "mqtt://test.mosquitto.org"; 
const MQTT_TOPIC = "tamperguard/ESP32_TamperGuard_001/alert";

// ------------------- MongoDB Cloud Setup -------------------
// Using the exact Atlas SRV string provided
const MONGO_URL = "mongodb+srv://email2sanjayv:gaming123@quickguard.ss1vv.mongodb.net/";
const DB_NAME = "sanjay057_qgbeta"; 

const mongoClient = new MongoClient(MONGO_URL);
let db;

// ------------------- Connect to Database -------------------
async function connectMongo() {
  try {
    await mongoClient.connect();
    db = mongoClient.db(DB_NAME);
    console.log(`✅ Connected to MongoDB Atlas (Database: ${DB_NAME})`);
  } catch (error) {
    console.error("❌ MongoDB Connection Error:", error);
  }
}

connectMongo();

// ------------------- Connect to MQTT -------------------
const mqttClient = mqtt.connect(MQTT_URL, { connectTimeout: 5000 });

mqttClient.on("connect", () => {
  console.log("✅ Connected to Mosquitto Cloud Broker");
  mqttClient.subscribe(MQTT_TOPIC, (err) => {
    if (!err) {
      console.log(`✅ Subscribed to topic: ${MQTT_TOPIC}`);
    } else {
      console.error("❌ MQTT Subscription Error:", err);
    }
  });
});

// ------------------- MQTT Error Handling -------------------
mqttClient.on("error", (err) => {
  console.error("❌ MQTT Error:", err.message);
});

mqttClient.on("reconnect", () => {
  console.log("⚠️ Reconnecting to MQTT...");
});

// ------------------- Handle Incoming Alerts -------------------
mqttClient.on("message", async (topic, message) => {
  try {
    const data = JSON.parse(message.toString());
    console.log("📩 Tamper Received:", data);

    // Insert the alert into your cloud database
    await db.collection("alerts").insertOne({
      ...data,
      received_at: new Date()
    });

    console.log("✅ Inserted into MongoDB Atlas");
  } catch (error) {
    console.error("❌ Error processing message:", error);
  }
});