# 🌫️ ArduinoUno-AirQuality-FusionSystem

## 📌 Descripción del Proyecto

Este proyecto consiste en el diseño e implementación de un prototipo embebido IoT de bajo costo para el monitoreo en tiempo real de la calidad del aire en la región Sabana Centro (Cundinamarca, Colombia).

El sistema integra múltiples variables ambientales críticas (material particulado, gases y condiciones meteorológicas) y aplica una lógica de fusión de datos para generar alertas tempranas in situ, sin utilizar redes de comunicación, cumpliendo con las restricciones técnicas del reto académico.

El sistema está desarrollado utilizando un Arduino Uno como microcontrolador principal.

---

## 🎯 Objetivo

Diseñar e implementar un sistema embebido capaz de:

- Medir material particulado (PM2.5 / PM10)
- Detectar gases contaminantes
- Registrar variables meteorológicas (temperatura, humedad y presión)
- Integrar múltiples señales mediante lógica de fusión ambiental
- Generar alertas locales en tiempo real mediante actuadores físicos

---

## 🧠 Arquitectura del Sistema

El sistema se organiza en tres niveles funcionales que permiten separar medición, análisis y respuesta:

### 1️⃣ Capa de Sensado
Encargada de capturar las variables ambientales:
- PMS5003 → Material particulado (UART)
- MQ135 → Concentración de gases (Entrada analógica)
- BME280 → Temperatura, Humedad y Presión (I2C)
Cada sensor entrega datos independientes que luego son procesados por el microcontrolador.

### 2️⃣ Capa de Procesamiento
El Arduino Uno realiza:
- Lectura periódica de sensores
- Validación básica de datos
- Cálculo de un ínidice compuesto de la calidad del aire
- Clasificación del estado del aire mediante umbrales
La lógica implementada combina las variables con diferentes niveles de importancia, dando mayor peso al material particulado y a los gases por su impacto directo en la salud.

### 3️⃣ Capa de Actuación e Interfaz
Permite comunicar el estado del aire al usuario mediante:
- Pantalla LCD (visualización alternada de variables y estado)
- Buzzer (alerta sonora según nivel de riesgo)
- LED RGB (indicador visual del estado del aire)
El sistema actualiza la información de manera continua sin bloquear la ejecución general.
---

## 🚫 Restricciones de Diseño

🔌 Restricción Energética
El sistema requiere alimentación externa (5V vía USB), por lo que no es completamente autónomo. No se implementó sistema de baterías dentro del alcance del proyecto.

⏳ Restricción Temporal
La disponibilidad de sensores dependía de tiempos de envío internacionales. Esto condicionó la selección de componentes y la planificación del montaje.

💰 Restricción Económica
Aunque el reto establece un límite inferior a 100 USD, el equipo estableció un presupuesto máximo de 50 USD, lo cual influyó en la selección de componentes accesibles y de bajo costo.

📐 Restricción de Espacio
El montaje se realizó con un Arduino Uno y una protoboard compacta, definiendo el tamaño mínimo físico del sistema y limitando su miniaturización.

📡 Restricción Funcional del Reto

No se permite Raspberry Pi

No se permiten redes de comunicación

Las alertas deben ser exclusivamente locales (visuales y sonoras)

---

## 📊 Clasificación de la Calidad del Aire

El sistema calcula un índice compuesto a partir de:

- Concentración de PM2.5
- Nivel estimado de gases
- Temperatura
- Humedad

Con base en este valor, el aire se clasifica en:

🟢 Bueno

🟡 Moderado

🔴 Peligroso

Cada categoría activa un patrón específico de LED y buzzer, permitiendo identificar el nivel de riesgo de forma inmediata.
---

## 🧪 Validación Experimental

Se realizaron pruebas variando de manera controlada:

- Incremento de gases (simulación con alcohol)
- Aumento de material particulado
- Cambios en condiciones ambientales

Se verificó que:

- El sistema clasifica correctamente el estado del aire
- Las alertas se activan según el nivel detectado
- La visualización se mantiene estable y actualizada
---

## 🛠️ Hardware Utilizado

- Arduino Uno
- Sensor PMS5003
- Sensor MQ135
- Sensor BME280
- Pantalla LCD I2C
- Buzzer activo
- LED RGB

---

## 👥 Integrantes

- Brainer Steven Jimenez Gonzalez
- Bruno Elias Pérez Merino

Curso: Internet de las Cosas  
Facultad de Ingeniería  
Universidad de La Sabana  
2026-1  

---

## 🎥 Video Demostrativo

[Insertar enlace al video en MS Teams]

---

## 📚 Documentación Técnica Completa

La documentación técnica completa del proyecto (arquitectura detallada, diagramas, modelo de fusión, resultados y análisis) se encuentra en la sección Wiki del repositorio.

👉 [Ir a la Wiki]

---

## 📌 Estado del Proyecto

✔ Prototipo funcional  
✔ Lógica de fusión implementada  
✔ Sistema de alertas validado  
✔ Documentación técnica en desarrollo  

---
